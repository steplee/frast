#pragma once

#include <lmdb.h>

#include <string>
#include <array>
#include <chrono>
#include <thread>

class Image;

extern double _encodeTime;
extern double _decodeTime;
extern double _imgMergeTime;
extern double _dbWriteTime;
extern double _dbReadTime;
extern double _dbEndTxnTime;
extern double _totalTime;

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

class Dataset {
	public:
		enum class OpenMode {
			READ_ONLY,
			READ_WRITE
		};

		Dataset(OpenMode m, const std::string& path, const DatabaseOptions& dopts=DatabaseOptions{});
		~Dataset();

		bool rasterio(Image& out, double aoiUwm[4]) const;

		bool get(Image& out, const BlockCoordinate& coord, MDB_txn** txn);
		int get_(MDB_val& out, const BlockCoordinate& coord, MDB_txn* txn);
		bool put(Image& in,  const BlockCoordinate& coord, MDB_txn** txn, bool allowOverwrite=false);
		int put_(MDB_val& in,  const BlockCoordinate& coord, MDB_txn* txn, bool allowOverwrite);


		bool createLevelIfNeeded(int lvl);

		bool beginTxn(MDB_txn**, bool readOnly=false);
		bool endTxn(MDB_txn**, bool abort=false);

	private:
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
		MDB_txn* w_txns[1];
		// TODO: have one w_txn per level (dbi)

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
