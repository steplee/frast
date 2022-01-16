#pragma once

#include <lmdb.h>

#include <string>
#include <array>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <cassert>

#include "image.h"


extern double _encodeTime;
extern double _decodeTime;
extern double _imgMergeTime;
extern double _dbWriteTime;
extern double _dbReadTime;
extern double _dbEndTxnTime;
extern double _totalTime;

void printDebugTimes();

template <class T>
double getMicroDiff(T b, T a) {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count() * 1e-3;
}
std::string prettyPrintMicros(double us);

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

		Dataset(const std::string& path, const DatabaseOptions& dopts=DatabaseOptions{}, OpenMode m=OpenMode::READ_ONLY);
		~Dataset();

		bool rasterio(Image& out, double aoiUwm[4]) const;

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

	protected:
		std::string path;
		bool readOnly;
		bool doStop = false;

		MDB_env *env = nullptr;

		void open_all_dbs();

		// TODO: Have a per dbi header with this info.
		double extent[4];

		MDB_dbi dbs[32];

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
		NoCommand, BeginLvl, EndLvl
	} cmd = NoCommand;
	union Data {
		int32_t lvl;
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
	inline bool push_back(T& t) {
		//if (w_idx - r_idx >= cap) return false;
		if (w_idx - r_idx >= cap) { assert(false); }
		data[w_idx % cap] = t;
		//printf(" - ring buffer push_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		w_idx++;
		return true;
	}
	inline int size()  const { return w_idx -  r_idx; }
	inline int empty() const { return w_idx == r_idx; }
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

		void push(WritableTile& tile);
		void sendCommand(const Command& cmd);

	private:
		constexpr static int MAX_THREADS = 8;
		int tilew, tileh;
		int numWorkers, buffersPerWorker, nBuffers;

		void w_loop();
		MDB_txn *w_txn;
		std::thread w_thread;
		std::condition_variable w_cv;
		std::mutex w_mtx;
		std::vector<WritableTile> tileBuffers; // We will have exactly numThreads * buffersPerWorker elements.
		// Sequence number of lended buffer.
		atomic_int tileBufferLendedIdx[MAX_THREADS];
		// Sequence number of commited-to-database image.
		atomic_int tileBufferCommittedIdx[MAX_THREADS];

		// A worker pushes the index of the buffer to this list. It never grows larger then N
		RingBuffer<int> pushedTileIdxs;
		std::vector<Command> waitingCommands;

};

