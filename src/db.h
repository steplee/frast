#pragma once

extern "C" {
#include <lmdb.h>
}

#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <cassert>

#include "utils/common.h"
#include "utils/data_structures.hpp"
#include "utils/timer.hpp"
#include "image.h"


class DatasetReaderIterator;

constexpr int MAX_LVLS = 26;
constexpr int MAX_READER_THREADS = 4;
constexpr int READER_TILE_CACHE_SIZE = 64;
constexpr int WRITER_CACHE_CAPACITY = 16 /* times nthreads */;
constexpr uint32_t BAD_LEVEL_CHOSEN = 55;

// Note: WebMercator the size of the map is actually 2x this.
constexpr double WebMercatorMapScale = 20037508.342789248;
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

extern AtomicTimer t_encodeImage, t_decodeImage, t_mergeImage,
			t_dbWrite, t_dbRead, t_dbBeginTxn, t_dbEndTxn, t_tileBufferCopy,
			t_rasterIo, t_fetchBlocks, t_warp, t_memcpyStrided,
			t_getCached, t_solve, t_gdal,
			t_total;
void printDebugTimes(); // Not used currenlty: descructors print automatically


struct BlockCoordinate {
	uint64_t c;
	inline BlockCoordinate(uint64_t cc) : c(cc) {}
	inline BlockCoordinate(const BlockCoordinate& bc) : c(bc.c) {}
#if 1
	inline BlockCoordinate(uint64_t z, uint64_t y, uint64_t x) : c(z<<58 | y<<29 | x) {}
	inline uint64_t z() const { return (c >> 58) & 0b111111; }
	inline uint64_t y() const { return (c >> 29) & 0b11111111111111111111111111111; }
	inline uint64_t x() const { return (c      ) & 0b11111111111111111111111111111; }
#else
	inline BlockCoordinate(uint64_t z, uint64_t y, uint64_t x) : c(x<<35 | y<<6 | (z&0b111111)) {}
	inline uint64_t z() const { return (c      ) & 0b111111; }
	inline uint64_t y() const { return (c >>  6) & 0b11111111111111111111111111111; }
	inline uint64_t x() const { return (c >> 35) & 0b11111111111111111111111111111; }
#endif
	inline bool operator==(const BlockCoordinate& other) const { return c == other.c; }
	inline operator uint64_t() const { return c; }
	inline operator const uint64_t*() const { return &c; }
	inline operator void*() const { return (void*) &c; }
};
static_assert(sizeof(BlockCoordinate) == 8);

constexpr static unsigned int INVALID_DB = 2147483648;

struct DatabaseOptions {
	//int64_t mapSize = 10485760l; // lmdb default: 10MB
	//uint64_t mapSize = 30lu * (1lu << 30lu); // x 1GB
	uint64_t mapSize = 100lu * (1lu << 30lu); // x 1GB
	bool threadLocalStorage = true;
	bool allowInflate = false;
};

struct DatasetMeta {
	// A region is considered a contiguous span of area.
	// It is recorded at only non-overview levels.
	struct Region {
		double tlbr[4] = {0};
	};
	struct LevelMeta {
		uint64_t tlbr[4] = {0};
		uint64_t nTiles = 0;
	};
	struct FixedSizeMeta {
		LevelMeta levelMetas[MAX_LVLS];
		//uint32_t channels;
		uint32_t format;
		uint32_t tileSize;
		uint64_t mapSize;
	} fixedSizeMeta;
	std::vector<Region> regions;

	// The union of all regions.
	Region computeCoveredRegion() const;
};





/*
 *
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

		bool get(Image& out, const BlockCoordinate& coord, MDB_txn** txn);
		bool getInflate_(Image& out, const BlockCoordinate& coord, MDB_txn* txn);
		bool get(std::vector<uint8_t>& out, const BlockCoordinate& coord, MDB_txn** txn); // By re-using output buffer, allocations will stop happening eventually
		int get_(MDB_val& out, const BlockCoordinate& coord, MDB_txn* txn);
		int put(Image& in,  const BlockCoordinate& coord, MDB_txn** txn, bool allowOverwrite=false);
		int put(const uint8_t* in, size_t len, const BlockCoordinate& coord, MDB_txn** txn, bool allowOverwrite=false);
		int put_(MDB_val& in,  const BlockCoordinate& coord, MDB_txn* txn, bool allowOverwrite);

		bool erase(const BlockCoordinate& coord, MDB_txn* txn);

		bool tileExists(const BlockCoordinate& bc, MDB_txn* txn);
		bool hasLevel(int lvl) const;


		bool createLevelIfNeeded(int lvl);

		bool beginTxn(MDB_txn**, bool readOnly=false) const;
		bool endTxn(MDB_txn**, bool abort=false) const;

		uint64_t determineLevelAABB(uint64_t tlbr[4], int lvl) const;

		void getExistingLevels(std::vector<int>& out) const;

		template <class F> bool iterLevel(int lvl, MDB_txn* txn, F&& f) {
			if (dbs[lvl] == INVALID_DB) {
				printf(" - iterLevel called on invalid lvl %d\n", lvl);
				return 1;
			}
			MDB_cursor* cursor;
			MDB_val key, val;
			if (mdb_cursor_open(txn, dbs[lvl], &cursor))
				throw std::runtime_error("Failed to open cursor.");
			if (mdb_cursor_get(cursor, &key, &val, MDB_FIRST)) {
				printf(" - iterLevel all on empty db lvl\n");
				return 1;
			} else {
				BlockCoordinate coord(*static_cast<uint64_t*>(key.mv_data));
				f(coord, val);
			}
			while (not mdb_cursor_get(cursor, &key, &val, MDB_NEXT)) {
				BlockCoordinate coord(*static_cast<uint64_t*>(key.mv_data));
				f(coord, val);
			}
			return 0;
		}

		int dropLvl(int lvl, MDB_txn* txn);

		// TODO: Read this from header, it is not done right now.

		// This is a slow and naive function. Only call after a modifying app is finished
		// doing stuff on all levels.
		// TODO: Replace with something more elegant
		void recompute_meta_and_write_slow(MDB_txn* txn);

		void setFormat(int32_t c) { meta.fixedSizeMeta.format = c; }
		void setTileSize(int32_t c) { meta.fixedSizeMeta.tileSize = c; }
		inline Image::Format format() { return (Image::Format)meta.fixedSizeMeta.format; }
		inline int32_t channels() { return Image::format2c(format()); }
		inline int32_t tileSize() { return meta.fixedSizeMeta.tileSize; }

		inline const DatasetMeta& getMeta() const { return meta; }

	protected:
		friend class DatasetReaderIterator;
		std::string path;
		bool readOnly;
		bool allowInflate = false;
		bool doStop = false;

		MDB_env *env = nullptr;

		void open_all_dbs();

		// TODO: Have a per dbi header with this info.
		double extent[4];

		MDB_dbi dbs[MAX_LVLS];
		MDB_dbi metaDb;


		DatasetMeta meta;

	private:
		// Just put this in main() for any program you care about.
		//std::unique_ptr<AtomicTimerMeasurement> _t_total = std::make_unique<AtomicTimerMeasurement>(t_total);

		// Called when opening.
		void decode_meta_(MDB_txn* txn);

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
	BlockCoordinate coord;
	std::vector<uint8_t> eimg;
	int bufferIdx;

	WritableTile(WritableTile&&)                 = delete;
	WritableTile& operator=(const WritableTile&) = delete;

	inline WritableTile() : coord(0,0,0)                          { }
	inline WritableTile& operator=(WritableTile&& other)          { copyFrom(other); return *this; }
	inline WritableTile(const WritableTile& other) : coord(0,0,0) { copyFrom(other); }

	void copyFrom(const WritableTile& tile);
	void fillWith(const BlockCoordinate& c, const MDB_val& val);
};

/*
 * Simple type to help an app control the DatasetWritable writer thread asynchronously.
 */
struct Command {
	enum Type : int32_t {
		NoCommand, BeginLvl, EndLvl, EraseLvl, TileReady, TileReadyOverwrite
	} cmd = NoCommand;
	union Data {
		int32_t lvl;
		int32_t tileBufferIdx;
	} data = Data{.lvl=0};
};

/*
 * This makes the assumption that no workers commit any of the same tiles!
 * (At least, inbetween BeginLvl and EndLvl commands)
 * This is because only one lonnnng write transaction is held the entire duration.
 *
 * You must also call sendCommand with StartLvl and EndLvl when starting/ending a new pyramid level of writing.
 *
 *  This also has a tile cache like DatasetReader does.
 *  Unlike DatasetReader::tileCache, however, each cache should be *thread local*.
 *  It is not safe to use from other threads, and obviously within the current level being processed.
 *  TODO: barely tested the caching mechanism.
 *
 */
using atomic_int = std::atomic_int;
class DatasetWritable : public Dataset {
	public:
		DatasetWritable(const std::string& path, const DatabaseOptions& dopts=DatabaseOptions{});
		~DatasetWritable();

		// Must be called before writing anything.
		void configure(int numWorkerThreads, int buffersPerWorker);

		// By having buffers for each thread seperately, we can avoid locking.
		WritableTile& blockingGetTileBufferForThread(int thread);

		//void push(WritableTile& tile);
		void sendCommand(const Command& cmd);

		// True if 'EndLvl' has not been processed
		bool hasOpenWrite();
		// Wait until all commands processed
		void blockUntilEmptiedQueue();

		// Not thread safe!
		bool getCached(int tid, Image& out, const BlockCoordinate& coord, MDB_txn** txn);

		constexpr static int MAX_THREADS = 8;
	private:
		int numWorkers, buffersPerWorker, nBuffers;

		LruCache<uint64_t, Image> perThreadTileCache[MAX_THREADS];

		void w_loop();
		MDB_txn *w_txn = nullptr;
		std::thread w_thread;
		std::condition_variable w_cv;
		std::mutex w_mtx;
		std::vector<WritableTile> tileBuffers; // We will have exactly numThreads * buffersPerWorker elements.

		// Would be better to use one atomic int that stores the difference between lended/commited,
		// then does compare-and-swap, rather than locking.
		int tileBufferLendedIdx[MAX_THREADS];
		int tileBufferCommittedIdx[MAX_THREADS];
		std::mutex tileBufferIdxMtx[MAX_THREADS];

		// A worker pushes the index of the buffer to this list. It never grows larger then N.
		// If it would, sendCommand() blocks
		RingBuffer<Command> pushedCommands;

		// LMDB has bad performance for very large write transactions.
		// So break up large ones.
		int curTransactionWriteCount = 0;
		int maxTransactionWriteCount = 150'000;

};



struct DatasetReaderOptions : public DatabaseOptions {
	float oversampleRatio = 1.f;
	int maxSampleTiles = 8;
	bool forceGray = false;
	bool forceRgb = false;

	int nthreads = 0;
};


/*
 *
 * Can only access safely from ONE THREAD
 *
 * There are two levels of caching:
 *			1) fetchBlocks(), caches the last output. If the same input tlbr is passed, the last result is copied to the output buffer.
 *			2) getCached() has an LRU cache of READER_TILE_CACHE_SIZE entries.
 *
 * So fetchBlocks() (and the rasterIo* funcs) employ two-level caching.
 * Calls to getCached() that are new will incur a Dataset::get() call, which also do a jpeg decode.
 *
 * fetchBlocks() is not thread safe.
 * getCached() is thread-safe, but only to internal threads. cacheMtx is acquired if opts.nthreads > 1.
 *
 */
class DatasetReader : public Dataset {
	public:
		DatasetReader(const std::string& path, const DatasetReaderOptions& dopts=DatasetReaderOptions{});
		~DatasetReader();

		// Access an AABB in the projection coordinate system (e.g. WM)
		// Image should already be allocated.
		// false on success.
		bool rasterIo(Image& out, const double bbox[4]);
		int fetchBlocks(Image& out, uint64_t lvl, const uint64_t tlbr[4], MDB_txn* txn0);
		// Access a quad in the projection coordinate system (e.g. WM)
		bool rasterIoQuad(Image& out, const double quad[8]);

		// Shared by all threads
		bool getCached(Image& out, const BlockCoordinate& coord, MDB_txn** txn);

	private:
		DatasetReaderOptions opts;
		Image accessCache;
		Image accessCache1;
		// TODO: have LruCache have a getView() method, then keep LruCache locked until memcpyStridedOutputFlatInput is run, then unlock.
		// This avoids a copy, at the cost of a critical region (or not cost if nthreads=0)
		//LruCache<uint64_t, Image> tileCache;
		LruCache<uint64_t, Image> tileCache;
		Image fetchedCache;
		uint64_t fetchedCacheBox[4];
		int fetchedCacheMissing = 0;

		// Only used if nthread>1
		std::mutex tileCacheMtx;

		MDB_txn* r_txns[MAX_READER_THREADS];
		std::thread threads[MAX_READER_THREADS];
		std::thread::id threadIds[MAX_READER_THREADS];
		void loaderThreadLoop(int idx);
		bool loadTile(Image& out);

		inline int getReaderThreadIdx() const {
			if (not readOnly) return -1;
			auto id = std::this_thread::get_id();
			for (int i=0; i<MAX_READER_THREADS; i++) if (id == threadIds[i]) return i;
			return -1;
		}

		// the first func finds without insepcting which tiles exist in the database.
		// the second func finds the best level that *actually covers* the AABB.
		// the 'dataDependent' ensures:
		//			1) The tiles needed do exist on that level, otherwise go up until they do.
		//			2) The sample size is not too large for the output buffer (passed as outCapacity)
		uint64_t findBestLvlForBoxAndRes(int imgH, int imgW, const double bboxWm[4]);
		uint64_t findBestLvlAndTlbr_dataDependent(uint64_t tileTlbr[4], uint32_t outCapacity, int imgH, int imgW, const double bboxWm[4], MDB_txn* txn);
		uint64_t findBestLvlAndTlbr_dataDependent(uint64_t tileTlbr[4], uint32_t outCapacity, int imgH, int imgW, float edgeLen, const double bboxWm[4], MDB_txn* txn);

};

