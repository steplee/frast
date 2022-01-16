#include "db.h"
#include "image.h"

#include <lmdb.h>
#include <cassert>
#include <string>

#include <sys/stat.h>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <array>
#include <iomanip>


/*
 * LMDB observations:
 *		- Using a single txn for many writes is MUCH faster
 *		- Using a single txn for many reads is marginally faster, if at all.
 */

std::atomic<double> _encodeTime = 0;
std::atomic<double> _decodeTime = 0;
std::atomic<double> _imgMergeTime = 0;
double _dbWriteTime = 0;
double _dbReadTime = 0;
double _dbEndTxnTime = 0;
double _totalTime = 0;
std::atomic<double> _tileBufferCopyTime = 0;

void printDebugTimes() {
	std::cout << " - Timing 'encodeTime'   : " << prettyPrintNanos(_encodeTime) << " (" << (_encodeTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'decodeTime'   : " << prettyPrintNanos(_decodeTime) << " (" << (_decodeTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'imgMergeTime' : " << prettyPrintNanos(_imgMergeTime) << " (" << (_imgMergeTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'dbWriteTime'  : " << prettyPrintNanos(_dbWriteTime) << " (" << (_dbWriteTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'dbReadTime'   : " << prettyPrintNanos(_dbReadTime) << " (" << (_dbReadTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'dbEndTxnTime' : " << prettyPrintNanos(_dbEndTxnTime) << " (" << (_dbEndTxnTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'tileBufCopy'  : " << prettyPrintNanos(_tileBufferCopyTime) << " (" << (_tileBufferCopyTime/_totalTime) * 100 << "%)\n";
}
std::string prettyPrintNanos(double ns) {
	std::string                  out = "";
	if (ns < 1'000)              out = std::to_string(ns) + "μs";
	if (ns < 1'000'000)          out = std::to_string(ns/1e3) + "μs";
	else if (ns < 1'000'000'000) out = std::to_string(ns/1e6) + "ms";
	else                         out = std::to_string(ns/1e9) + "s";
	return out;
}


Dataset::Dataset(const std::string& path, const DatabaseOptions& dopts, OpenMode m)
	: path(path),
	  readOnly(m == OpenMode::READ_ONLY)
{
	for (int i=0; i<32; i++) dbs[i] = INVALID_DB;
	for (int i=0; i<NumReadThreads+1; i++) r_txns[i] = 0;

	if (auto err = mdb_env_create(&env)) {
		throw std::runtime_error(std::string{"mdb_env_create failed with "} + mdb_strerror(err));
	}

	mdb_env_set_mapsize(env, dopts.mapSize);

	mdb_env_set_maxdbs(env, 32);

	int flags = 0;
	if (readOnly) flags = MDB_RDONLY;
	mode_t fileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
	if (auto err = mdb_env_open(env, path.c_str(), flags, fileMode)) {
		throw std::runtime_error(std::string{"mdb_env_open failed with "} + mdb_strerror(err));
	}

	open_all_dbs();

	if (readOnly)
		for (int i=0; i<NumReadThreads; i++) {
			threads[i] = std::thread(&Dataset::loaderThreadLoop, this, i);
			threadIds[i] = threads[i].get_id();
		}
}

bool Dataset::beginTxn(MDB_txn** txn, bool readOnly) const {
	int flags = readOnly ? MDB_RDONLY : 0;
	auto err = mdb_txn_begin(env, nullptr, flags, txn);
	if (err)
		throw std::runtime_error(std::string{"(beginTxn) mdb_txn_begin failed with "} + mdb_strerror(err));
	return err != 0;
}
bool Dataset::endTxn(MDB_txn** txn, bool abort) const {
	AddTimeGuard g(_dbEndTxnTime);
	if (abort)
		mdb_txn_abort(*txn);
	else
		mdb_txn_commit(*txn);
	*txn = nullptr;
	return true;
}

void Dataset::open_all_dbs() {
	MDB_txn *txn;

	int txn_flags = MDB_RDONLY;
	if (auto err = mdb_txn_begin(env, nullptr, txn_flags, &txn)) {
		throw std::runtime_error(std::string{"(open_all_dbs) mdb_txn_begin failed with "} + mdb_strerror(err));
	}

	for (int i=0; i<32; i++) {
		std::string name = std::string{"lvl"} + std::to_string(i);
		int flags = 0;
		if (auto err = mdb_dbi_open(txn, name.c_str(), flags, dbs+i))
			//throw std::runtime_error(std::string{"(open_all_dbs) mdb_dbi_open failed with "} + mdb_strerror(err));
			//std::cout << " - (open_all_dbs) failed to open lvl " << i << "\n";
			{}
		else
			std::cout << " - (open_all_dbs) opened lvl " << i << "\n";
	}

	mdb_txn_commit(txn);
}

bool Dataset::createLevelIfNeeded(int lvl) {
	if (dbs[lvl] != INVALID_DB) return false;
	if (readOnly) throw std::runtime_error("Can only call createLevelIfNeeded() if not readOnly.");

	std::cout << " - (createLevelIfNeeded) creating lvl " << lvl << "\n";


	MDB_txn *txn;
	if (auto err = mdb_txn_begin(env, nullptr, 0, &txn))
		throw std::runtime_error(std::string{"(createLevelIfNeeded) mdb_txn_begin failed with "} + mdb_strerror(err));

	std::string name = "lvl" + std::to_string(lvl);
	mdb_dbi_open(txn, name.c_str(), MDB_CREATE, dbs+lvl);
	mdb_txn_commit(txn);
	return true;
}

void Dataset::loaderThreadLoop(int idx) {
	if (auto err = mdb_txn_begin(env, nullptr, MDB_RDONLY, r_txns+idx)) {
		std::cout << " - (Dataset::Dataset()) failed to open r_txn " << idx << "\n";
	} else
		std::cout << " - (Dataset::Dataset()) opened  r_txn " << idx << " -> " << r_txns[idx] << "\n";

	while (not doStop) sleep(1);
}

Dataset::~Dataset() {
	doStop = true;
	for (int i=0; i<NumReadThreads; i++) if (threads[i].joinable()) threads[i].join();
	mdb_env_close(env);
}



bool Dataset::get(Image& out, const BlockCoordinate& coord, MDB_txn** givenTxn) {
	MDB_txn* theTxn;

	if (givenTxn == nullptr) {
		int threadIdx = getReaderThreadIdx();
		//std::cout << " - using txn " << getReaderThreadIdx() << " -> " << theTxn << "\n";
		if (threadIdx >= 0) {
			theTxn = r_txns[getReaderThreadIdx()];
		} else if (auto err = mdb_txn_begin(env, nullptr, MDB_RDONLY, &theTxn))
			throw std::runtime_error(std::string{"(get) mdb_txn_begin failed with "} + mdb_strerror(err));
	} else theTxn = *givenTxn;

	MDB_val eimg_;
	bool ret;
	{
		AddTimeGuard g(_dbReadTime);
		ret = get_(eimg_, coord, theTxn);
	}

	if (ret) return ret != 0;

	EncodedImageRef eimg { eimg_.mv_size, (uint8_t*) eimg_.mv_data };
	{
		AddTimeGuardAsync g(_decodeTime);
		ret = decode(out, eimg);
	}

	if (givenTxn == nullptr) endTxn(&theTxn);
	return ret != 0;
}

int Dataset::get_(MDB_val& val, const BlockCoordinate& coord, MDB_txn* txn) {
	MDB_val key { 8, (void*)(&coord.c) };
	if (auto err = mdb_get(txn, dbs[coord.z()], &key, &val)) {
		std::cout << " - mdb_get error: " << mdb_strerror(err) << "\n";
		return err;
	}
	return 0;
}
int Dataset::put_(MDB_val& val,  const BlockCoordinate& coord, MDB_txn* txn, bool allowOverwrite) {
	MDB_val key { 8, (void*)(&coord.c) };

	//std::cout << " - writing keylen " << key.mv_size << " vallen " << val.mv_size << "\n";
	{
		AddTimeGuard g(_dbWriteTime);
		//std::cout << " - writing keylen " << key.mv_size << " vallen " << val.mv_size << " to lvl " << coord.z() << "\n";

		auto err = mdb_put(txn, dbs[coord.z()], &key, &val, allowOverwrite ? 0 : MDB_NOOVERWRITE);
		return err;
	}
}

int Dataset::put(Image& in,  const BlockCoordinate& coord, MDB_txn** givenTxn, bool allowOverwrite) {

	assert(false); // deprecated: the worker threads should do the encoding!


	// XXX NOTE:
	// If the key is already present and we must merge, this function encodes the image the first time for no reason.
	// If you expect many conflicts (for example, merging two tiffs of overlapping aoi), then this function
	// should mdb_get to see if the key exists and avoid the first encode!

	//MDB_val val;
	EncodedImage eimg;
	{
		AddTimeGuardAsync g(_encodeTime);
		encode(eimg, in);

		//val = MDB_val{ eimg.size(), static_cast<void*>(eimg.data()) };
	}

	//auto err = put_(val, coord, theTxn, allowOverwrite);
	return put(eimg.data(), in.size(), coord, givenTxn, allowOverwrite);


}

int Dataset::put(const uint8_t* in, size_t len, const BlockCoordinate& coord, MDB_txn** givenTxn, bool allowOverwrite) {
	MDB_txn* theTxn;

	if (givenTxn == nullptr) {
		if (auto err = mdb_txn_begin(env, nullptr, 0, &theTxn))
			throw std::runtime_error(std::string{"(put) mdb_txn_begin failed with "} + mdb_strerror(err));
	} else
		theTxn = *givenTxn;

	MDB_val val { len, ((void*)in) };
	auto err = put_(val, coord, theTxn, allowOverwrite);

	/*
	if (err == MDB_KEYEXIST) {
		std::cout << " - Tile existing, attempting to merge (" << coord.z() << " " << coord.y() << " " << coord.x() << ")\n";
		Image tmp { in.h, in.w, in.format};
		tmp.alloc();
		if (get(tmp, coord, &theTxn))
			throw std::runtime_error("(put)(get) Tried to get existing key (to merge images), but failed!");

		{
			AddTimeGuard g(_imgMergeTime);
			//tmp.add_nodata__average_(in);
			tmp.add_nodata__keep_(in);
		}

		if (put(tmp, coord, &theTxn, true))
			throw std::runtime_error("(put)(put) Tried to put merged image, but failed!");
	}
	*/
	if (err == MDB_KEYEXIST) {
		printf(" - duplicate coord %ld %ld %ld.\n", coord.z(), coord.y(), coord.x());
		assert(false);
	}

	else if (err == MDB_MAP_FULL and givenTxn != nullptr) {
		throw std::runtime_error("DB grew too large! Must increase size!");
		// Commit, then transparently create new transaction, and re-run
		//mdb_txn_commit(*givenTxn);
		//std::cout << " - (put) [silently commiting and recreating input txn]\n";
		//beginTxn(givenTxn);
		//theTxn = *givenTxn;
		//err = put_(val, coord, theTxn);
	} else if (err) std::cout << " - mdb_put error: " << mdb_strerror(err) << "\n";

	if (givenTxn == nullptr) endTxn(&theTxn);
	return err != 0;
}


bool Dataset::rasterio(Image& out, double aoiUwm[4]) const {
	return true;
}

uint64_t Dataset::determineLevelAABB(uint64_t tlbr[4], int lvl) const {
	tlbr[0] = tlbr[2] = 0;
	tlbr[1] = tlbr[3] = 0;

	if (dbs[lvl] == INVALID_DB) {
		printf(" - determineLevelAABB called on invalid lvl %d\n", lvl);
		return 0;
	}

	MDB_txn *txn;
	if (beginTxn(&txn, true))
		throw std::runtime_error("Failed to open txn.");

	MDB_cursor* cursor;
	MDB_val key, val;

	if (mdb_cursor_open(txn, dbs[lvl], &cursor))
		throw std::runtime_error("Failed to open cursor.");

	int nn = 0;

	if (mdb_cursor_get(cursor, &key, &val, MDB_FIRST)) {
		printf(" - determineLevelAABB searched (NO FIRST ENTRY) %d\n", nn);
		return 0;
	} else {
		BlockCoordinate coord(*static_cast<uint64_t*>(key.mv_data));
		tlbr[0] = tlbr[2] = coord.x();
		tlbr[1] = tlbr[3] = coord.y();
		nn++;
	}

	while (not mdb_cursor_get(cursor, &key, &val, MDB_NEXT)) {
		BlockCoordinate coord(*static_cast<uint64_t*>(key.mv_data));

		uint64_t x = coord.x();
		uint64_t y = coord.y();
		if (x < tlbr[0]) tlbr[0] = x;
		if (y < tlbr[1]) tlbr[1] = y;
		if (x > tlbr[2]) tlbr[2] = x;
		if (y > tlbr[3]) tlbr[3] = y;
		nn++;
	}

	printf(" - determineLevelAABB searched %d\n", nn);
	return (tlbr[2]-tlbr[0]) * (tlbr[3]-tlbr[1]);
}







void WritableTile::copyFrom(const WritableTile& tile) {
	AddTimeGuardAsync tg(_tileBufferCopyTime);
	bufferIdx = tile.bufferIdx;
	image.copyFrom(tile.image);
	if (tile.eimg.size() > eimg.size()) {
		eimg.resize(tile.eimg.size() * 2);
		std::copy(tile.eimg.begin(), tile.eimg.end(), eimg.begin());
	}
	coord = tile.coord;
}
void WritableTile::fillWith(const Image& im, const BlockCoordinate& c, const std::vector<uint8_t>& v) {
	AddTimeGuardAsync tg(_tileBufferCopyTime);
	image.copyFrom(im);
	if (v.size() > eimg.size()) {
		eimg.resize(v.size() * 2);
		std::copy(v.begin(), v.end(), eimg.begin());
	}
	coord = c;
}


DatasetWritable::~DatasetWritable() {
	doStop = true;
	w_cv.notify_one();
	if (w_thread.joinable()) w_thread.join();
}

void DatasetWritable::w_loop() {
	//beginTxn(&w_txn, false);

	while (!doStop) {
		int theTileIdx;
		int nAvailable = 0;
		Command theCommand;
		theCommand.cmd = Command::NoCommand;
		{
			std::unique_lock<std::mutex> lck(w_mtx);
			w_cv.wait(lck, [this]{return doStop or pushedTileIdxs.size() or waitingCommands.size();});

			if (waitingCommands.size()) {
				theCommand = waitingCommands.back();
				waitingCommands.pop_back();
			}

			nAvailable = pushedTileIdxs.size();
			if (nAvailable > 0)
				pushedTileIdxs.pop_front(theTileIdx);
			//if (pushedTileIdxs.pop_front(theTileIdx) == false) { printf(" - Weird: awoke and found a >0 size, but could not pop.\n"); fflush(stdout); exit(1); }
		}

		//printf(" - (awoke to %d avail) handling push of tile %d\n", nAvailable, theTileIdx); fflush(stdout);

		if (theCommand.cmd != Command::NoCommand) {
			if (theCommand.cmd == Command::BeginLvl) {
				printf(" - recv command to start lvl %d\n", theCommand.data.lvl); fflush(stdout);
				if (w_txn) endTxn(&w_txn);
				this->createLevelIfNeeded(theCommand.data.lvl);
				beginTxn(&w_txn, false);
			} else if (theCommand.cmd == Command::EndLvl) {
				printf(" - recv command to end lvl %d\n", theCommand.data.lvl); fflush(stdout);
				assert(w_txn);
				endTxn(&w_txn);
				//beginTxn(&w_txn, false);
			}
		}

		if (doStop and nAvailable == 0) break;

		if (nAvailable) {
			WritableTile& tile = tileBuffers[theTileIdx];
			//printf(" - Popped tile %d, coord %lu %lu %lu, data size %lu\n", theTileIdx, tile.coord.z(), tile.coord.y(), tile.coord.x(), tile.eimg.size());
			this->put(tile.eimg.data(), tile.eimg.size(), tile.coord, &w_txn, false);

			//++(tileBufferCommittedIdx[theTileIdx / numWorkers]);
			int theWorker = theTileIdx / buffersPerWorker;
			tileBufferCommittedIdx[theWorker] += 1;
			//printf(" - handled ... incremented idxs [thr %d] are %dcmt %dlnd\n", theWorker, tileBufferCommittedIdx[theWorker].load(), tileBufferLendedIdx[theWorker].load()); fflush(stdout);

			if (nAvailable > 1 or theCommand.cmd != Command::NoCommand or doStop) w_cv.notify_one();
		}
	}

	//if (w_txn) endTxn(&w_txn);
}

// Note: this ASSUMES the correct thread is calling the func.
WritableTile& DatasetWritable::blockingGetTileBufferForThread(int thread) {
	int lendIdx = tileBufferLendedIdx[thread];
	while(1) {
		lendIdx = tileBufferLendedIdx[thread];
		int comtIdx = tileBufferCommittedIdx[thread];
		if (lendIdx - comtIdx < buffersPerWorker) {
			//printf(" - (blockingGetTileBufferForThread : got open buffer [thr %d] (cmt %d, lnd %d, nbuf %d) -> %d\n",
				//thread, comtIdx, lendIdx, buffersPerWorker, lendIdx%buffersPerWorker);
			break;
		}

		usleep(2'000);
		//usleep(200'000);
		printf(" - (blockingGetTileBufferForThread : too many buffers lent [thr %d] (cmt %d, lnd %d, nbuf %d), waiting...\n",
				thread, comtIdx, lendIdx, buffersPerWorker);
	}
	tileBufferLendedIdx[thread] += 1;
	return tileBuffers[thread * buffersPerWorker + lendIdx % buffersPerWorker];
}

void DatasetWritable::configure(int tilew, int tileh, int tilec, int numWorkerThreads, int buffersPerWorker) {
	assert(numWorkerThreads < MAX_THREADS);
	this->tilew = tilew;
	this->tileh = tileh;
	this->numWorkers = numWorkerThreads;
	this->buffersPerWorker = buffersPerWorker;

	nBuffers = numWorkers * buffersPerWorker;
	tileBuffers.resize(nBuffers);
	for (int i=0; i<nBuffers; i++) {
		tileBufferLendedIdx[i] = 0;
		tileBufferCommittedIdx[i] = 0;
		tileBuffers[i].image = Image { tilew, tileh, tilec };
		tileBuffers[i].image.calloc();
		tileBuffers[i].bufferIdx = i;
		//printf(" - made buffer tile with idx %d\n", tileBuffers[i].bufferIdx);
	}

	pushedTileIdxs = RingBuffer<int>(nBuffers);
	w_thread = std::thread(&DatasetWritable::w_loop, this);
}

void DatasetWritable::push(WritableTile& tile) {
	{
		std::unique_lock<std::mutex> lck(w_mtx);
		pushedTileIdxs.push_back(tile.bufferIdx);
		//printf(" - push buffer tile with idx %d [thr %d]\n", tile.bufferIdx, tile.bufferIdx/buffersPerWorker);
	}
	w_cv.notify_one();
}

DatasetWritable::DatasetWritable(const std::string& path, const DatabaseOptions& dopts)
	: Dataset(path, dopts, Dataset::OpenMode::READ_WRITE) {
}


void DatasetWritable::sendCommand(const Command& cmd) {

	//if (cmd.cmd == Command::EndLvl)
	usleep(500'000);

	{
		if (cmd.cmd == Command::EndLvl) printf(" - Acquiring w_mtx to EndLvl.\n"); fflush(stdout);
		std::lock_guard<std::mutex> lck(w_mtx);
		if (cmd.cmd == Command::EndLvl) printf(" - Pushing EndLvl.\n"); fflush(stdout);
		waitingCommands.push_back(cmd);
	}
	w_cv.notify_one();

	/*
	if (cmd.cmd == Command::EndLvl)
		while (w_txn) {
			printf(" - sendCommand() waiting on EndLvl w_txn to end.\n");
			usleep(500'000);
		}
		*/
	if (cmd.cmd == Command::EndLvl)
		while (w_txn) {
			printf(" - sendCommand() waiting on EndLvl w_txn to end.\n");
			usleep(500'000);
		}
}

