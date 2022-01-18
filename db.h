#pragma once

extern "C" {
#include <lmdb.h>
}

#include <string>
#include <array>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <cassert>

#include "image.h"

constexpr int MAX_LVLS = 26;
constexpr int MAX_READER_THREADS = 4;

// Note: WebMercator the size of the map is actually 2x this.
constexpr double WebMercatorScale = 20037508.342789248;
// Level 0 holds 2*WebMercatorScale, Level 1 half that, and so on.
constexpr double WebMercatorCellSizes[MAX_LVLS] = {
	40075016.685578495, 20037508.342789248, 10018754.171394624,
	5009377.085697312, 2504688.542848656, 1252344.271424328,
	626172.135712164, 313086.067856082, 156543.033928041,
	78271.5169640205, 39135.75848201025, 19567.879241005125,
	9783.939620502562, 4891.969810251281, 2445.9849051256406,
	1222.9924525628203, 611.4962262814101, 305.7481131407051,
	152.87405657035254, 76.43702828517627, 38.218514142588134,
	19.109257071294067, 9.554628535647034, 4.777314267823517,
	2.3886571339117584, 1.1943285669558792
};
constexpr float WebMercatorCellSizesf[MAX_LVLS] = {
	40075016.685578495, 20037508.342789248, 10018754.171394624,
	5009377.085697312, 2504688.542848656, 1252344.271424328,
	626172.135712164, 313086.067856082, 156543.033928041,
	78271.5169640205, 39135.75848201025, 19567.879241005125,
	9783.939620502562, 4891.969810251281, 2445.9849051256406,
	1222.9924525628203, 611.4962262814101, 305.7481131407051,
	152.87405657035254, 76.43702828517627, 38.218514142588134,
	19.109257071294067, 9.554628535647034, 4.777314267823517,
	2.3886571339117584, 1.1943285669558792
};

// Assumes channelStride = 1.
template <int channels>
inline void memcpyStridedOutputFlatInput(uint8_t* dst, uint8_t* src, size_t rowStride, size_t w, size_t h) {
	for (int y=0; y<h; y++)
	for (int x=0; x<w; x++)
	for (int c=0; c<channels; c++) {
		dst[y*rowStride*channels + x*channels + c] = src[y*w*channels+x*channels+c];
	}
}


extern std::atomic<double> _encodeTime;
extern std::atomic<double> _decodeTime;
extern std::atomic<double> _imgMergeTime;
extern double _dbWriteTime;
extern double _dbReadTime;
extern double _dbEndTxnTime;
extern double _totalTime;
extern std::atomic<double> _tileBufferCopyTime;
void printDebugTimes();

template <class T>
double getNanoDiff(T b, T a) {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count() * 1e-3;
}
std::string prettyPrintNanos(double us);

struct AddTimeGuard {
	std::chrono::time_point<std::chrono::high_resolution_clock> st;
	double& acc;
	inline AddTimeGuard(double& acc) : acc(acc) {
		st = std::chrono::high_resolution_clock::now();
	}
	inline ~AddTimeGuard() {
		auto et = std::chrono::high_resolution_clock::now();
		acc += std::chrono::duration_cast<std::chrono::nanoseconds>(et-st).count();
	}
};
struct AddTimeGuardAsync {
	std::chrono::time_point<std::chrono::high_resolution_clock> st;
	std::atomic<double>& acc;
	inline AddTimeGuardAsync(std::atomic<double>& acc) : acc(acc) {
		st = std::chrono::high_resolution_clock::now();
	}
	inline ~AddTimeGuardAsync() {
		auto et = std::chrono::high_resolution_clock::now();
		double old = acc.load();
		acc = old + std::chrono::duration_cast<std::chrono::nanoseconds>(et-st).count();
	}
};

struct BlockCoordinate {
	uint64_t c;
	inline BlockCoordinate(uint64_t cc) : c(cc) {}
	inline BlockCoordinate(const BlockCoordinate& bc) : c(bc.c) {}
	inline BlockCoordinate(uint64_t z, uint64_t y, uint64_t x) : c(z<<58 | y<<29 | x) {}
	inline uint64_t z() const { return (c >> 58) & 0b111111; }
	inline uint64_t y() const { return (c >> 29) & 0b11111111111111111111111111111; }
	inline uint64_t x() const { return (c      ) & 0b11111111111111111111111111111; }
	inline bool operator==(const BlockCoordinate& other) const { return c == other.c; }
	inline operator uint64_t() const { return c; }
	inline operator const uint64_t*() const { return &c; }
	inline operator void*() const { return (void*) &c; }
};
static_assert(sizeof(BlockCoordinate) == 8);

constexpr static unsigned int INVALID_DB = 2147483648;

struct DatabaseOptions {
	//int64_t mapSize = 10485760l; // lmdb default: 10MB
	//int64_t mapSize = 10485760l * 8l; // 80MB
	int64_t mapSize = 10485760l * 9000l; // 1GB
};


/*
 *
 * TODO: Do not have r_txns in this class, and erase all reader specific code.
 *       Instead there should be a subclass that does all that, just like there is 'DatasetWritable'
 *
 */

class Dataset {
	public:
		enum class OpenMode {
			READ_ONLY,
			READ_WRITE
		};

		struct __attribute__((packed)) LevelHeader {
			bool valid : 1;
			int32_t tlbr[4];
		};
		struct __attribute__((packed)) DatasetHeader {
			LevelHeader lvlHeaders[MAX_LVLS];
			int channels;
			int tileSize;
		};

		Dataset(const std::string& path, const DatabaseOptions& dopts=DatabaseOptions{}, OpenMode m=OpenMode::READ_ONLY);
		~Dataset();

		bool get(Image& out, const BlockCoordinate& coord, MDB_txn** txn);
		bool get(std::vector<uint8_t>& out, const BlockCoordinate& coord, MDB_txn** txn); // By re-using output buffer, allocations will stop happening eventually
		int get_(MDB_val& out, const BlockCoordinate& coord, MDB_txn* txn);
		int put(Image& in,  const BlockCoordinate& coord, MDB_txn** txn, bool allowOverwrite=false);
		int put(const uint8_t* in, size_t len, const BlockCoordinate& coord, MDB_txn** txn, bool allowOverwrite=false);
		int put_(MDB_val& in,  const BlockCoordinate& coord, MDB_txn* txn, bool allowOverwrite);


		bool createLevelIfNeeded(int lvl);

		bool beginTxn(MDB_txn**, bool readOnly=false) const;
		bool endTxn(MDB_txn**, bool abort=false) const;

		uint64_t determineLevelAABB(uint64_t tlbr[4], int lvl) const;

		void getExistingLevels(std::vector<int>& out) const;

	protected:
		std::string path;
		bool readOnly;
		bool doStop = false;

		// TODO: Read this from header, it is not done right now.
		int channels = 3;
		int tileSize = 256;

		MDB_env *env = nullptr;

		void open_all_dbs();

		// TODO: Have a per dbi header with this info.
		double extent[4];

		MDB_dbi dbs[MAX_LVLS];

		// When in readOnly mode, we have long-lived dedicated per-thread read transactions.
		// Note: they are not used in write mode.
		static constexpr int NumReadThreads = 2;
		MDB_txn *r_txns[NumReadThreads];

		std::array<std::thread, NumReadThreads> threads;
		std::array<std::thread::id, NumReadThreads> threadIds;
		void loaderThreadLoop(int idx);

		inline int getReaderThreadIdx() const {
			if (not readOnly) return -1;
			auto id = std::this_thread::get_id();
			for (int i=0; i<NumReadThreads; i++) if (id == threadIds[i]) return i;
			return -1;
		}

};


/*
 * I do not use move-semantics here because DatasetWritable holds the buffers, and we want
 * to avoid Image/vector re-allocations.
 * Instead when the worker (e.g. warper) threads wish to push a Tile to be stored,
 * they ask for its destination and the buffers are re-used if they are large enough
 * (otherwise e.g. the vector will be resized to be larger, but this will stop happening
 *  eventually)
 */
struct WritableTile {
	Image image;
	BlockCoordinate coord;
	std::vector<uint8_t> eimg;
	int bufferIdx;

	WritableTile(WritableTile&&)                 = delete;
	WritableTile& operator=(const WritableTile&) = delete;

	inline WritableTile() : coord(0,0,0)                          { }
	inline WritableTile& operator=(WritableTile&& other)          { copyFrom(other); return *this; }
	inline WritableTile(const WritableTile& other) : coord(0,0,0) { copyFrom(other); }

	void copyFrom(const WritableTile& tile);
	void fillWith(const Image& im, const BlockCoordinate& c, const std::vector<uint8_t>& v);
};

/*
 * Simple type to help an app control the DatasetWritable writer thread asynchronously.
 *
 */
struct Command {
	enum Type : int32_t {
		NoCommand, BeginLvl, EndLvl, EraseLvl, TileReady
	} cmd = NoCommand;
	union Data {
		int32_t lvl;
		int32_t tileBufferIdx;
	} data = Data{.lvl=0};
};

template <class T>
struct RingBuffer {
	std::vector<T> data;
	int cap, w_idx=0, r_idx=0;
	RingBuffer() : cap(0) { }
	RingBuffer(int cap_) : cap(cap_) {
		data.resize(cap);
	}
	inline bool pop_front(T& t) {
		if (r_idx == w_idx) return false;
		t = data[r_idx % cap];
		//printf(" - ring buffer pop_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		r_idx++;
		return true;
	}
	inline bool push_back(const T& t) {
		//if (w_idx - r_idx >= cap) return false;
		if (w_idx - r_idx >= cap) { assert(false); }
		data[w_idx % cap] = t;
		//printf(" - ring buffer push_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		w_idx++;
		return true;
	}
	inline int size()  const { return w_idx -  r_idx; }
	inline int empty() const { return w_idx == r_idx; }
	inline bool isFull() const { return w_idx - r_idx >= cap; }
};

/*
 * This makes the assumption that no workers commit any of the same tiles!
 * This is because only one lonnnng write transaction is held the entire duration.
 *
 * You must also call sendCommand with StartLvl and EndLvl when starting/ending a new pyramid level of writing.
 * Techincally this is broken, since the commands could be re-ordered against pushed tiles.
 * So when sending EndLvl, the callee will sleep for 1 second.
 */
using atomic_int = std::atomic_int;
class DatasetWritable : public Dataset {
	public:
		DatasetWritable(const std::string& path, const DatabaseOptions& dopts=DatabaseOptions{});
		~DatasetWritable();

		// Must be called before writing anything.
		void configure(int tilew, int tileh, int tilec, int numWorkerThreads, int buffersPerWorker);

		// By having buffers for each thread seperately, we can avoid locking.
		WritableTile& blockingGetTileBufferForThread(int thread);

		//void push(WritableTile& tile);
		void sendCommand(const Command& cmd);

		// True if 'EndLvl' has not been processed
		bool hasOpenWrite();

		constexpr static int MAX_THREADS = 8;
	private:
		int tilew, tileh;
		int numWorkers, buffersPerWorker, nBuffers;

		void w_loop();
		MDB_txn *w_txn = nullptr;
		std::thread w_thread;
		std::condition_variable w_cv;
		std::mutex w_mtx;
		std::vector<WritableTile> tileBuffers; // We will have exactly numThreads * buffersPerWorker elements.
		// Sequence number of lended buffer &
		// Sequence number of commited-to-database image.
		// Originally I used these atomic ints, but I thought they caused a bug, which they weren't.
		// Still I'm not convinced it is correct, since they are not atomic w.r.t eachother.
		//atomic_int tileBufferLendedIdx[MAX_THREADS];
		//atomic_int tileBufferCommittedIdx[MAX_THREADS];
		int tileBufferLendedIdx[MAX_THREADS];
		int tileBufferCommittedIdx[MAX_THREADS];
		std::mutex tileBufferIdxMtx[MAX_THREADS];

;
		// A worker pushes the index of the buffer to this list. It never grows larger then N
		//RingBuffer<int> pushedTileIdxs;
		//std::vector<Command> waitingCommands;
		RingBuffer<Command> pushedCommands;

};



struct DatasetReaderOptions : public DatabaseOptions {
	float oversampleRatio = 1.f;
	int maxSampleTiles = 5;
	bool forceGray = false;
	bool forceRgb = false;

	int nthreads = 0;
};


/*
 *
 * Can only access safely from ONE THREAD
 *
 */
class DatasetReader : public Dataset {
	public:
		DatasetReader(const std::string& path, const DatasetReaderOptions& dopts=DatasetReaderOptions{});

		// Image should already be allocated.
		// false on success.
		bool rasterIo(Image& out, const double bbox[4]);

	private:
		DatasetReaderOptions opts;
		Image accessCache;
		Image accessCache1;

};

uint64_t findBestLvlForBoxAndRes(int imgH, int imgW, int tileSize, const double bboxWm[4]);
