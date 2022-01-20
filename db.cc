#include "db.h"
#include "image.h"

#include <cassert>
#include <string>

#include <sys/stat.h>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <array>
#include <iomanip>
#include <cmath>

#define DEBUG_RASTERIO
#ifdef DEBUG_RASTERIO
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#endif

// Needed for rasterIoQuad.
// TODO: Replace with simple DLT solve, using eigen cholesky solve.
#include <opencv2/imgproc.hpp>


/*
 * LMDB observations:
 *		- Using a single txn for many writes is MUCH faster
 *		- Using a single txn for many reads is marginally faster, if at all.
 */

	/*
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
	*/

AtomicTimer t_total("total"),
			t_encodeImage("encodeImage"), t_decodeImage("decodeImage"), t_mergeImage("mergeImage"),
			t_dbWrite("dbWrite"), t_dbRead("dbRead"), t_dbEndTxn("dbEndTxn"), t_tileBufferCopy("tileBufferCopy");
void printDebugTimes() {}


/* ===================================================
 *
 *
 *                  Dataset
 *
 *
 * =================================================== */



Dataset::Dataset(const std::string& path, const DatabaseOptions& dopts, OpenMode m)
	: path(path),
	  readOnly(m == OpenMode::READ_ONLY)
{
	for (int i=0; i<MAX_LVLS; i++) dbs[i] = INVALID_DB;

	if (auto err = mdb_env_create(&env)) {
		throw std::runtime_error(std::string{"mdb_env_create failed with "} + mdb_strerror(err));
	}

	mdb_env_set_mapsize(env, dopts.mapSize);

	mdb_env_set_maxdbs(env, MAX_LVLS+1);

	int flags = 0;
	if (readOnly) flags = MDB_RDONLY;
	mode_t fileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
	if (auto err = mdb_env_open(env, path.c_str(), flags, fileMode)) {
		throw std::runtime_error(std::string{"mdb_env_open failed with "} + mdb_strerror(err));
	}

	open_all_dbs();

}

bool Dataset::beginTxn(MDB_txn** txn, bool readOnly) const {
	int flags = readOnly ? MDB_RDONLY : 0;
	auto err = mdb_txn_begin(env, nullptr, flags, txn);
	if (err)
		throw std::runtime_error(std::string{"(beginTxn) mdb_txn_begin failed with "} + mdb_strerror(err));
	return err != 0;
}
bool Dataset::endTxn(MDB_txn** txn, bool abort) const {
	AtomicTimerMeasurement g(t_dbEndTxn);
	if (abort)
		mdb_txn_abort(*txn);
	else
		mdb_txn_commit(*txn);
	*txn = nullptr;
	return false;
}

void Dataset::open_all_dbs() {
	MDB_txn *txn;

	int txn_flags = MDB_RDONLY;
	if (auto err = mdb_txn_begin(env, nullptr, txn_flags, &txn)) {
		throw std::runtime_error(std::string{"(open_all_dbs) mdb_txn_begin failed with "} + mdb_strerror(err));
	}

	for (int i=0; i<MAX_LVLS; i++) {
		std::string name = std::string{"lvl"} + std::to_string(i);
		int flags = 0;
		if (auto err = mdb_dbi_open(txn, name.c_str(), flags, dbs+i))
			//throw std::runtime_error(std::string{"(open_all_dbs) mdb_dbi_open failed with "} + mdb_strerror(err));
			//std::cout << " - (open_all_dbs) failed to open lvl " << i << "\n";
			{}
		else {
			//std::cout << " - (open_all_dbs) opened lvl " << i << "\n";
		}
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


Dataset::~Dataset() {
	doStop = true;
	assert(env);
	mdb_env_close(env);
	env = 0;
	//printf (" - (~Dataset closed mdb env)\n");
}



bool Dataset::get(Image& out, const BlockCoordinate& coord, MDB_txn** givenTxn) {
	MDB_txn* theTxn;

	if (givenTxn == nullptr) {
		if (auto err = mdb_txn_begin(env, nullptr, MDB_RDONLY, &theTxn))
			throw std::runtime_error(std::string{"(get) mdb_txn_begin failed with "} + mdb_strerror(err));
	} else theTxn = *givenTxn;

	MDB_val eimg_;
	bool ret = get_(eimg_, coord, theTxn);

	if (ret) {
		if (givenTxn == nullptr) endTxn(&theTxn);
		return ret != 0;
	}

	EncodedImageRef eimg { eimg_.mv_size, (uint8_t*) eimg_.mv_data };
	{
		AtomicTimerMeasurement g(t_decodeImage);
		ret = decode(out, eimg);
	}

	if (givenTxn == nullptr) endTxn(&theTxn);
	return ret != 0;
}

int Dataset::get_(MDB_val& val, const BlockCoordinate& coord, MDB_txn* txn) {
	AtomicTimerMeasurement g(t_dbRead);

	MDB_val key { 8, (void*)(&coord.c) };
	if (auto err = mdb_get(txn, dbs[coord.z()], &key, &val)) {
		//std::cout << " - mdb_get error: " << mdb_strerror(err) << " (block " << coord.z() << "z " << coord.y() << "y " << coord.x() << "x)\n";
		return err;
	}
	return 0;
}
int Dataset::put_(MDB_val& val,  const BlockCoordinate& coord, MDB_txn* txn, bool allowOverwrite) {
	AtomicTimerMeasurement g(t_dbWrite);

	MDB_val key { 8, (void*)(&coord.c) };

	//std::cout << " - writing keylen " << key.mv_size << " vallen " << val.mv_size << "\n";
	{
		AtomicTimerMeasurement g(t_dbWrite);
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
		AtomicTimerMeasurement g(t_encodeImage);
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

int Dataset::dropLvl(int lvl, MDB_txn* txn) {
		int ret = mdb_drop(txn, dbs[lvl], 1);
		dbs[lvl] = INVALID_DB;
		return ret;
}

bool Dataset::erase(const BlockCoordinate& coord, MDB_txn* txn) {

	MDB_val key { 8, static_cast<void*>(coord) };
	if (auto err = mdb_del(txn, dbs[coord.z()], &key, nullptr))
		return true;

	return false;
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

	if (endTxn(&txn))
		throw std::runtime_error("Failed to close txn.");

	//printf(" - determineLevelAABB searched %d\n", nn);
	return (tlbr[2]-tlbr[0]) * (tlbr[3]-tlbr[1]);
}

void Dataset::getExistingLevels(std::vector<int>& out) const {
	for (int lvl=0; lvl<MAX_LVLS; lvl++)
		if (dbs[lvl] != INVALID_DB)
			out.push_back(lvl);
}

bool Dataset::tileExists(const BlockCoordinate& bc, MDB_txn* txn) {
	uint64_t lvl = bc.z();
	if (dbs[lvl] == INVALID_DB) return false;
	MDB_val key, val;
	key.mv_data = (void*) &(bc.c);
	key.mv_size = sizeof(BlockCoordinate);
	if (auto err = mdb_get(txn, dbs[lvl], &key, &val)) {
		if (err == MDB_NOTFOUND) return false;
		else throw std::runtime_error(std::string{"(tileExists) mdb err "} + mdb_strerror(err));
	}
	return true;
}

bool Dataset::hasLevel(int lvl) const {
	return dbs[lvl] != INVALID_DB;
}




/* ===================================================
 *
 *
 *                  DatasetWritable
 *
 *
 * =================================================== */




void WritableTile::copyFrom(const WritableTile& tile) {
	AtomicTimerMeasurement g(t_tileBufferCopy);
	bufferIdx = tile.bufferIdx;
	image.copyFrom(tile.image);
	if (tile.eimg.size() > eimg.size()) {
		eimg.resize(tile.eimg.size() * 2);
		std::copy(tile.eimg.begin(), tile.eimg.end(), eimg.begin());
	}
	coord = tile.coord;
}
void WritableTile::fillWith(const Image& im, const BlockCoordinate& c, const std::vector<uint8_t>& v) {
	AtomicTimerMeasurement g(t_tileBufferCopy);
	image.copyFrom(im);
	if (v.size() > eimg.size()) {
		eimg.resize(v.size() * 2);
		std::copy(v.begin(), v.end(), eimg.begin());
	}
	coord = c;
}
void WritableTile::fillWith(const BlockCoordinate& c, const MDB_val& val) {
	AtomicTimerMeasurement g(t_tileBufferCopy);
	if (val.mv_size > eimg.size()) {
		eimg.resize(val.mv_size * 2);
		std::copy((uint8_t*)val.mv_data, ((uint8_t*)val.mv_data)+val.mv_size, eimg.begin());
	}
	coord = c;
}


DatasetWritable::~DatasetWritable() {
	doStop = true;
	w_cv.notify_one();
	if (w_thread.joinable()) w_thread.join();
	printf (" - (~DatasetWritable join w_thread)\n");
}

void DatasetWritable::w_loop() {
	//beginTxn(&w_txn, false);

	while (true) {
		int nWaitingCommands = 0;
		Command theCommand;
		theCommand.cmd = Command::NoCommand;
		{
			std::unique_lock<std::mutex> lck(w_mtx);
			
			w_cv.wait(lck, [this]{return doStop or pushedCommands.size();});
			nWaitingCommands = pushedCommands.size();
			if (nWaitingCommands) {
				//theCommand = pushedCommands.pop_back();
				pushedCommands.pop_front(theCommand);
			}

			if (doStop and nWaitingCommands == 0) {
				printf(" - (wthread exiting.)\n");
				break;
			}
			if (!doStop and nWaitingCommands == 0) {
				printf(" - (wthread, spurious wakeup.\n");
			}


			// Lock the mutex if creating or ending a level.
			// The TileReady command needn't hold mutex.
			if (theCommand.cmd == Command::BeginLvl) {
				printf(" - recv command to start lvl %d\n", theCommand.data.lvl); fflush(stdout);
				if (w_txn) endTxn(&w_txn);
				this->createLevelIfNeeded(theCommand.data.lvl);
				beginTxn(&w_txn, false);
				nWaitingCommands--;
			} else if (theCommand.cmd == Command::EndLvl) {
				printf(" - recv command to end lvl %d\n", theCommand.data.lvl); fflush(stdout);
				assert(w_txn);
				endTxn(&w_txn);
				//beginTxn(&w_txn, false);
				nWaitingCommands--;
			} else if (theCommand.cmd == Command::EraseLvl) {
				printf(" - recv command to erase lvl %d\n", theCommand.data.lvl); fflush(stdout);
				if (w_txn) {
					printf(" - Cannot have open w_txn while erasing level. Should sent 'EndLvl' first.\n"); fflush(stdout);
					exit(1);
				}
				beginTxn(&w_txn, false);
				dropLvl(theCommand.data.lvl, w_txn);
				endTxn(&w_txn);
				nWaitingCommands--;
			}
		}

		//printf(" - (awoke to %d avail) handling push of tile %d\n", nAvailable, theTileIdx); fflush(stdout);

		if (theCommand.cmd != Command::NoCommand) {
			if (theCommand.cmd == Command::TileReady) {
				int theTileIdx = theCommand.data.tileBufferIdx;
				int theWorker = theTileIdx / buffersPerWorker;
				//printf(" - recv command to commit tilebuf %d (worker %d, buf %d), nWaiting: %d\n", theCommand.data.tileBufferIdx, theWorker, theTileIdx, nWaitingCommands); fflush(stdout);
				WritableTile& tile = tileBuffers[theTileIdx];
				this->put(tile.eimg.data(), tile.eimg.size(), tile.coord, &w_txn, false);

				tileBufferIdxMtx[theWorker].lock();
				tileBufferCommittedIdx[theWorker] += 1;
				tileBufferIdxMtx[theWorker].unlock();
			}
			nWaitingCommands--;
		}


		if (doStop or nWaitingCommands > 0) w_cv.notify_one();
	}

	//if (w_txn) endTxn(&w_txn);
}

// Note: this ASSUMES the correct thread is calling the func.
WritableTile& DatasetWritable::blockingGetTileBufferForThread(int thread) {
	int nwaited = 0;

	while (1) {
		tileBufferIdxMtx[thread].lock();
		int lendIdx = tileBufferLendedIdx[thread];
		int comtIdx = tileBufferCommittedIdx[thread];

		if (lendIdx - comtIdx < buffersPerWorker) {
			tileBufferLendedIdx[thread] += 1;
			tileBufferIdxMtx[thread].unlock();
			return tileBuffers[thread * buffersPerWorker + lendIdx % buffersPerWorker];
		} else {
			tileBufferIdxMtx[thread].unlock();

			if (nwaited++ % 1 == 0)
				printf(" - (blockingGetTileBufferForThread : too many buffers lent [thr %d] (cmt %d, lnd %d, nbuf %d), waited %d times...\n",
						thread, comtIdx, lendIdx, buffersPerWorker, nwaited);
			usleep(2'000);
		}
	}
}

void DatasetWritable::configure(int numWorkerThreads, int buffersPerWorker) {
	assert(numWorkerThreads < MAX_THREADS);
	this->numWorkers = numWorkerThreads;
	this->buffersPerWorker = buffersPerWorker;

	nBuffers = numWorkers * buffersPerWorker;
	tileBuffers.resize(nBuffers);
	for (int i=0; i<numWorkerThreads; i++) {
		tileBufferLendedIdx[i] = 0;
		tileBufferCommittedIdx[i] = 0;
	}
	for (int i=0; i<nBuffers; i++) {
		tileBuffers[i].image = Image { tileSize, tileSize, channels };
		tileBuffers[i].image.calloc();
		tileBuffers[i].bufferIdx = i;
		//printf(" - made buffer tile with idx %d\n", tileBuffers[i].bufferIdx);
	}

	pushedCommands = RingBuffer<Command>(128);
	w_thread = std::thread(&DatasetWritable::w_loop, this);
}


DatasetWritable::DatasetWritable(const std::string& path, const DatabaseOptions& dopts)
	: Dataset(path, dopts, Dataset::OpenMode::READ_WRITE) {
}

bool DatasetWritable::hasOpenWrite() {
	std::unique_lock<std::mutex> lck(w_mtx);
	return (not pushedCommands.empty()) or w_txn != nullptr;
}

void DatasetWritable::sendCommand(const Command& cmd) {
	{
		std::unique_lock<std::mutex> lck(w_mtx);
		bool full = pushedCommands.isFull();
		while (full) {
			printf(" - commandQ full, sleeping.\n");
			lck.unlock();
			usleep(100'000);
			w_cv.notify_one();
			lck.lock();
			full = pushedCommands.isFull();
		}

		pushedCommands.push_back(cmd);
		lck.unlock();
		w_cv.notify_one();
	}

#if 0

	//if (cmd.cmd == Command::EndLvl)
	usleep(10'000);

	{
		//if (cmd.cmd == Command::EndLvl) printf(" - Acquiring w_mtx to EndLvl.\n"); fflush(stdout);
		std::lock_guard<std::mutex> lck(w_mtx);
		//if (cmd.cmd == Command::EndLvl) printf(" - Pushing EndLvl.\n"); fflush(stdout);
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
			usleep(100'000);
		}
#endif
}

void DatasetWritable::blockUntilEmptiedQueue() {
	bool empty = false;
	{
		std::lock_guard<std::mutex> lck(w_mtx);
		//printf(" - blockUntilEmptiedQueue :: size at start: %d\n", pushedCommands.size());
		empty = pushedCommands.empty();
	}
	while (not empty) {
		usleep(20'000);
		{
			std::lock_guard<std::mutex> lck(w_mtx);
			empty = pushedCommands.empty();
		}
	}
	//printf(" - blockUntilEmptiedQueue :: size at end: %d\n", pushedCommands.size());
}





/* ===================================================
 *
 *
 *                  DatasetReader
 *
 *
 * =================================================== */




DatasetReader::DatasetReader(const std::string& path, const DatasetReaderOptions& dopts)
	: Dataset(path, static_cast<const DatabaseOptions>(dopts), OpenMode::READ_ONLY),
	  opts(dopts),
	  tileCache(READER_TILE_CACHE_SIZE)
{

	assert(dopts.nthreads == 0); // only blocking loads suported rn

	for (int i=0; i<dopts.nthreads+1; i++) r_txns[i] = 0;
	for (int i=0; i<dopts.nthreads; i++) {
		threads[i] = std::thread(&DatasetReader::loaderThreadLoop, this, i);
		threadIds[i] = threads[i].get_id();
	}

	accessCache1 = Image {                      tileSize,                      tileSize, channels };
	accessCache  = Image { dopts.maxSampleTiles*tileSize, dopts.maxSampleTiles*tileSize, channels };
	accessCache1.calloc();
	accessCache .calloc();
}

DatasetReader::~DatasetReader() {
	doStop = true;
	for (int i=0; i<MAX_READER_THREADS; i++) if (threads[i].joinable()) threads[i].join();
}

void DatasetReader::loaderThreadLoop(int idx) {
	if (auto err = mdb_txn_begin(env, nullptr, MDB_RDONLY, r_txns+idx)) {
		std::cout << " - (DatasetReader::loaderThreadLoop()) failed to open r_txn " << idx << "\n";
	} else
		std::cout << " - (DatasetReader::loaderThreadLoop()) opened  r_txn " << idx << " -> " << r_txns[idx] << "\n";

	while (not doStop) sleep(1);
}

bool DatasetReader::loadTile(Image& out) {
		int threadIdx = getReaderThreadIdx();
		MDB_txn* theTxn;
		//std::cout << " - using txn " << getReaderThreadIdx() << " -> " << theTxn << "\n";
		if (threadIdx >= 0) {
			theTxn = r_txns[getReaderThreadIdx()];
		} else if (auto err = mdb_txn_begin(env, nullptr, MDB_RDONLY, &theTxn))
			throw std::runtime_error(std::string{"(get) mdb_txn_begin failed with "} + mdb_strerror(err));

		return false;
}

bool DatasetReader::rasterIoQuad(Image& out, const double quad[8]) {
	// Compute overview & tiles needed
	int ow = out.w;
	int oh = out.h;

	MDB_txn* r_txn_;
	if (auto err = beginTxn(&r_txn_, true))
		throw std::runtime_error(std::string{"(findBestLvlAndTlbr) mdb_txn_begin failed with "} + mdb_strerror(err));

	const double quadColMajor[8] = {
		quad[0], quad[2], quad[4], quad[6],
		quad[1], quad[3], quad[5], quad[7] };
	double bboxWm[4] = {
		*std::min_element(quadColMajor+0, quadColMajor+4),
		*std::min_element(quadColMajor+4, quadColMajor+8),
		*std::max_element(quadColMajor+0, quadColMajor+4),
		*std::max_element(quadColMajor+4, quadColMajor+8)
	};

	uint64_t tileTlbr[4];
	uint64_t lvl = findBestLvlAndTlbr_dataDependent(tileTlbr, oh,ow, bboxWm, r_txn_);
	double s = (.5 * (1<<lvl)) / WebMercatorScale;

	/*
	double sampledTlbr[4] = {
		std::floor((WebMercatorScale + bboxWm[0]) * s) / s - WebMercatorScale,
		std::floor((WebMercatorScale + bboxWm[1]) * s) / s - WebMercatorScale,
		std::ceil((WebMercatorScale + bboxWm[2]) * s) / s - WebMercatorScale,
		std::ceil((WebMercatorScale + bboxWm[3]) * s) / s  - WebMercatorScale};
	*/
	double sampledTlbr[4] = {
		((double)tileTlbr[0]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale,
		((double)tileTlbr[1]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale,
		((double)tileTlbr[2]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale,
		((double)tileTlbr[3]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale };

	int ny = tileTlbr[3] - tileTlbr[1], nx = tileTlbr[2] - tileTlbr[0];
	// sampled width/height
	int sw = nx*tileSize, sh = ny*tileSize;
	printf(" - (rasterIoQuad) sampling [%d %d tiles] [%d %d px] to fill outputBuffer of size [%d %d px]\n",
			ny,nx, sh,sw, oh,ow);

	// If >2 tiles, and enabled, use bg threads
	// In either case, after this, accessCache will have the needed tiles.
	fetchBlocks(accessCache, lvl, tileTlbr, r_txn_);

	if (auto err = endTxn(&r_txn_, true))
		throw std::runtime_error(std::string{"(findBestLvlAndTlbr) mdb_txn_end failed with "} + mdb_strerror(err));

	// Find the perspective transformation that takes the sampled image into the
	// queried quad.
	printf(" - sampledTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			sampledTlbr[0], sampledTlbr[1], sampledTlbr[2], sampledTlbr[3],
			sampledTlbr[2] - sampledTlbr[0], sampledTlbr[3] - sampledTlbr[1]);
	printf(" - queryTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			bboxWm[0], bboxWm[1], bboxWm[2], bboxWm[3], bboxWm[2] - bboxWm[0], bboxWm[3] - bboxWm[1]);

	float x_scale = sw / (sampledTlbr[2] - sampledTlbr[0]);
	float y_scale = sh / (sampledTlbr[3] - sampledTlbr[1]);
	float in_corners[8] = {
		// pt1
		((float)(quad[0] - sampledTlbr[0])) * x_scale,
		((float)(quad[1] - sampledTlbr[1])) * y_scale,
		// pt2
		((float)(quad[2] - sampledTlbr[0])) * x_scale,
		((float)(quad[3] - sampledTlbr[1])) * y_scale,
		// pt3
		((float)(quad[4] - sampledTlbr[0])) * x_scale,
		((float)(quad[5] - sampledTlbr[1])) * y_scale,
		// pt4
		((float)(quad[6] - sampledTlbr[0])) * x_scale,
		((float)(quad[7] - sampledTlbr[1])) * y_scale,
	};
	float out_corners[8] = {
		0, 0,
		(float) ow, 0,
		(float) ow, (float) oh,
		0, (float) oh };

	for (int i=0; i<4; i++) in_corners[i*2+1] = sh - 1 - in_corners[i*2+1];
	for (int i=0; i<4; i++) out_corners[i*2+1] = oh - out_corners[i*2+1];
	//printf(" - in_corners : %f %f -> %f %f\n", in_corners[0], in_corners[1], in_corners[2], in_corners[3]);
	//printf(" - out_corners: %f %f -> %f %f\n", out_corners[0], out_corners[1], out_corners[2], out_corners[3]);

#ifdef DEBUG_RASTERIO
	cv::Mat dbgImg1 = cv::Mat(sh, sw, accessCache.channels()==3?CV_8UC3:CV_8UC1, accessCache.buffer).clone();
	for (int y=0; y<ny; y++) dbgImg1(cv::Rect(0, y*tileSize, sw, 1)) = cv::Scalar{0};
	for (int x=0; x<nx; x++) dbgImg1(cv::Rect(x*tileSize, 0, 1, sh)) = cv::Scalar{0};
	for (int i=0; i<4; i++)
		//cv::circle(dbgImg1, cv::Point{(int)in_corners[2*i+0], (int)in_corners[2*i+1]}, 2, cv::Scalar{0,0,255}, -1);
		cv::line(dbgImg1,
				cv::Point{(int)in_corners[2*i+0], (int)in_corners[2*i+1]},
				cv::Point{(int)in_corners[2*((i+1)%4)+0], (int)in_corners[2*((i+1)%4)+1]},
				cv::Scalar{0,255,0}, 1);
	//cv::imwrite("out/rasterioSampled.jpg", dbgImg1);
	cv::imshow("debug", dbgImg1);
	cv::waitKey(1);
#endif

	cv::Mat a { 4, 2, CV_32F,  in_corners };
	cv::Mat b { 4, 2, CV_32F, out_corners };
	cv::Mat h = cv::getPerspectiveTransform(a,b);
	//cv::Mat h = cv::getPerspectiveTransform(b,a);
	if (h.type() == CV_64F) h.convertTo(h, CV_32F);
	float *H = (float*) h.data;
	printf(" - in_corners:\n %f %f\n %f %f\n %f %f\n %f %f\n",
			in_corners[0], in_corners[1],
			in_corners[2], in_corners[3],
			in_corners[4], in_corners[5],
			in_corners[6], in_corners[7]);
	printf(" - out_corners:\n %f %f\n %f %f\n %f %f\n %f %f\n",
			out_corners[0], out_corners[1],
			out_corners[2], out_corners[3],
			out_corners[4], out_corners[5],
			out_corners[6], out_corners[7]);

	// Warp affine must get correct sample w/h
	int push_w = accessCache.w, push_h = accessCache.h;
	accessCache.w = sw;
	accessCache.h = sh;
	printf(" - Warping %d %d %d -> %d %d %d\n",
			accessCache.w, accessCache.h, accessCache.channels(),
			out.w, out.h, out.channels());
	accessCache.warpPerspective(out, H);
	accessCache.w = push_w;
	accessCache.h = push_h;

	return false;

}

bool DatasetReader::rasterIo(Image& out, const double bboxWm[4]) {

	// Compute overview & tiles needed
	int ow = out.w;
	int oh = out.h;

	MDB_txn* r_txn_;
	if (auto err = beginTxn(&r_txn_, true))
		throw std::runtime_error(std::string{"(findBestLvlAndTlbr) mdb_txn_begin failed with "} + mdb_strerror(err));

	uint64_t tileTlbr[4];
	uint64_t lvl = findBestLvlAndTlbr_dataDependent(tileTlbr, oh,ow, bboxWm, r_txn_);
	double s = (.5 * (1<<lvl)) / WebMercatorScale;


	// double sampledTlbr[4] = {
		// std::floor(bboxWm[0] * s) / s,
		// std::floor(bboxWm[1] * s) / s,
		// std::ceil(bboxWm[2] * s) / s,
		// std::ceil(bboxWm[3] * s) / s };
	double sampledTlbr[4] = {
		((double)tileTlbr[0]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale,
		((double)tileTlbr[1]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale,
		((double)tileTlbr[2]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale,
		((double)tileTlbr[3]) * WebMercatorScale * 2. / (1<<lvl) - WebMercatorScale };

	int ny = tileTlbr[3] - tileTlbr[1], nx = tileTlbr[2] - tileTlbr[0];
	// sampled width/height
	int sw = nx*tileSize, sh = ny*tileSize;
	printf(" - (rasterIo) sampling [%d %d tiles] [%d %d px] to fill outputBuffer of size [%d %d px]\n",
			ny,nx, sh,sw, oh,ow);

	// If >2 tiles, and enabled, use bg threads
	// In either case, after this, accessCache will have the needed tiles.
	fetchBlocks(accessCache, lvl, tileTlbr, r_txn_);

	if (auto err = endTxn(&r_txn_, true))
		throw std::runtime_error(std::string{"(findBestLvlAndTlbr) mdb_txn_end failed with "} + mdb_strerror(err));

	// Find the affine transformation that takes the sampled image into the
	// queried bbox.
	printf(" - sampledTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			sampledTlbr[0], sampledTlbr[1], sampledTlbr[2], sampledTlbr[3],
			sampledTlbr[2] - sampledTlbr[0], sampledTlbr[3] - sampledTlbr[1]);
	printf(" - queryTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			bboxWm[0], bboxWm[1], bboxWm[2], bboxWm[3], bboxWm[2] - bboxWm[0], bboxWm[3] - bboxWm[1]);

	float in_corners[4] = {
		(float) ((bboxWm[0] - sampledTlbr[0]) * sw / (sampledTlbr[2] - sampledTlbr[0])),
		(float) ((bboxWm[1] - sampledTlbr[1]) * sh / (sampledTlbr[3] - sampledTlbr[1])),
		(float) ((bboxWm[2] - sampledTlbr[0]) * sw / (sampledTlbr[2] - sampledTlbr[0])),
		(float) ((bboxWm[3] - sampledTlbr[1]) * sh / (sampledTlbr[3] - sampledTlbr[1])),
	};
	float out_corners[4] = { 0, 0, (float) ow, (float) oh };
	//printf(" - in_corners : %f %f -> %f %f\n", in_corners[0], in_corners[1], in_corners[2], in_corners[3]);
	//printf(" - out_corners: %f %f -> %f %f\n", out_corners[0], out_corners[1], out_corners[2], out_corners[3]);
	float inset_tl_x_i = in_corners[0];
	float inset_tl_y_i = in_corners[1];
	float scale_x = out_corners[2] / (in_corners[2] - in_corners[0]);
	float scale_y = out_corners[3] / (in_corners[3] - in_corners[1]);
	float inset_tl_x = -in_corners[0] * scale_x;
	float inset_tl_y = -(sh - in_corners[3]) * scale_y;


	//printf(" - scale and offset: %f %f %f %f\n", scale_x, scale_y, inset_tl_x, inset_tl_y);
	assert(scale_x > 0.f);
	assert(scale_y > 0.f);
	assert(inset_tl_x <= 0.f); assert(inset_tl_y <= 0.f);
	float A[6] = {
		scale_x, 0, inset_tl_x,
		0, scale_y, inset_tl_y };

#ifdef DEBUG_RASTERIO
	cv::Mat dbgImg1 = cv::Mat(sh, sw, accessCache.channels()==3?CV_8UC3:CV_8UC1, accessCache.buffer).clone();
	for (int y=0; y<ny; y++) dbgImg1(cv::Rect(0, y*tileSize, sw, 1)) = cv::Scalar{0};
	for (int x=0; x<nx; x++) dbgImg1(cv::Rect(x*tileSize, 0, 1, sh)) = cv::Scalar{0};
	cv::Point pt1 { (int)(inset_tl_x_i), (int)(inset_tl_y_i) };
	cv::Point pt2 { (int)(inset_tl_x_i+scale_x*ow), (int)(inset_tl_y_i*scale_y*oh) };
	cv::rectangle(dbgImg1, pt1, pt2, cv::Scalar{255,0,0}, 2);
	float Ai[6] = {
		1.f/scale_x, 0, -inset_tl_x/scale_x,
		0, 1.f/scale_y, -inset_tl_y/scale_y };
	cv::Point pt1_ { (int)(Ai[0]*0+Ai[2]), (int)(Ai[4]*0+Ai[5]) };
	cv::Point pt2_ { (int)(Ai[0]*ow+Ai[2]), (int)(Ai[4]*oh+Ai[5]) };
	cv::rectangle(dbgImg1, pt1_, pt2_, cv::Scalar{0,255,0}, 2);
	//cv::imwrite("out/rasterioSampled.jpg", dbgImg1);
	cv::imshow("debug", dbgImg1);
	cv::waitKey(1);
#endif

	// Warp affine must get correct sample w/h
	int push_w = accessCache.w, push_h = accessCache.h;
	accessCache.w = sw;
	accessCache.h = sh;
	printf(" - Warping %d %d %d -> %d %d %d\n",
			accessCache.w, accessCache.h, accessCache.channels(),
			out.w, out.h, out.channels());
	accessCache.warpAffine(out, A);
	accessCache.w = push_w;
	accessCache.h = push_h;

	return false;

}

bool DatasetReader::getCached(Image& out, const BlockCoordinate& coord, MDB_txn** txn) {
	if (opts.nthreads > 1) tileCacheMtx.lock();
	if (tileCache.get(out, coord.c)) {
		printf(" - (getCached) cache hit for tile %luz %luy %lux\n", coord.z(), coord.y(), coord.x());
		if (opts.nthreads > 1) tileCacheMtx.unlock();
		return false;
	}
	if (opts.nthreads > 1) tileCacheMtx.unlock();
	if (get(out,coord,txn)) {
		// Failed. Do not cache.
		return true;
	}
	printf(" - (getCached) cache miss for tile %luz %luy %lux\n", coord.z(), coord.y(), coord.x());
	if (opts.nthreads > 1) tileCacheMtx.lock();
	tileCache.set(coord.c, out);
	if (opts.nthreads > 1) tileCacheMtx.unlock();
	return false;
}

// Returns number of invalid tiles.
int DatasetReader::fetchBlocks(Image& out, uint64_t lvl, const uint64_t tlbr[4], MDB_txn* txn0) {

	if (tlbr[0] == fetchedCacheBox[0] and
	    tlbr[1] == fetchedCacheBox[1] and
	    tlbr[2] == fetchedCacheBox[2] and
	    tlbr[3] == fetchedCacheBox[3]) {
		out = fetchedCache;
		printf(" - (fetchBlocks) cache hit.\n");
		return fetchedCacheMissing;
	}

	bool ownTxn = false;
	if (txn0 == nullptr) {
		if (auto err = beginTxn(&txn0, true))
			throw std::runtime_error(std::string{"(fetchBlocks) mdb_txn_begin failed with "} + mdb_strerror(err));
		ownTxn = true;
	}

	int32_t nx = tlbr[2] - tlbr[0];
	int32_t ny = tlbr[3] - tlbr[1];
	assert(out.w >= tileSize*nx);
	assert(out.h >= tileSize*ny);
	int sw = nx*tileSize; // Sampled width and height
	int sh = ny*tileSize;

	int nMissing = 0;

	// If >2 tiles, and enabled, use bg threads
	if (opts.nthreads == 0 or nx*ny <= 2) {
		for (int yi=0; yi<ny; yi++) {
		for (int xi=0; xi<nx; xi++) {
			BlockCoordinate tileCoord { lvl, tlbr[1]+yi, tlbr[0]+xi };
			if (getCached(accessCache1, tileCoord, &txn0)) {
				printf(" - Failed to get tile %lu %lu %lu\n", tileCoord.z(), tileCoord.y(), tileCoord.x());
				memset(accessCache1.buffer, 0, tileSize*tileSize*channels);
				nMissing++;
			}
			//printf(" - copy with offset %d %d\n", yi*sw*channels, xi*channels);
			uint8_t* dst = out.buffer + (ny-1-yi)*(tileSize*sw*channels) + xi*(tileSize*channels);
			if (channels == 1)      memcpyStridedOutputFlatInput<1>(dst, accessCache1.buffer, sw, tileSize, tileSize);
			else if (channels == 3) memcpyStridedOutputFlatInput<3>(dst, accessCache1.buffer, sw, tileSize, tileSize);
			else if (channels == 4) memcpyStridedOutputFlatInput<4>(dst, accessCache1.buffer, sw, tileSize, tileSize);
		}
		}
	} else {
		// TODO: Multi-threaded load.
		// ...
	}

	if (ownTxn)
		if (auto err = endTxn(&txn0))
			throw std::runtime_error(std::string{"(fetchBlocks) mdb_txn_end failed with "} + mdb_strerror(err));


	// Populate cache.
	fetchedCache = out;
	fetchedCacheMissing = nMissing;
	fetchedCacheBox[0] = tlbr[0];
	fetchedCacheBox[1] = tlbr[1];
	fetchedCacheBox[2] = tlbr[2];
	fetchedCacheBox[3] = tlbr[3];

	return nMissing;
}


static int floor_log2_i_(float x) {
	assert(x >= 0);

	// Could also use floating point log2.
	// Could also convert x to an int and use intrinsics.
	int i = 0;
	int xi = x * 4.f; // offset by 2^2 to get better resolution, if x < 1.
	//while ((1<<i) < xi) { i++; };
	while ((1<<(i+1)) < xi) { i++; };
	return i-2;
}

uint64_t DatasetReader::findBestLvlForBoxAndRes(int imgH, int imgW, const double bboxWm[4]) {
	assert(false); // deprecated

	// 1. Find optimal level
	// 2. Find closest level that exists in entire db

	// (1)
	double boxW = bboxWm[2] - bboxWm[0];
	double boxH = bboxWm[3] - bboxWm[1];
	float mean = (boxW + boxH) * .5f; // geometric mean makes more sense really.
	float x = (mean / static_cast<float>(std::min(imgH,imgW))) * (tileSize);
	int lvl_ = floor_log2_i_(WebMercatorCellSizesf[0] / x);
	printf(" - Given bboxWm %lfw %lfh, tileSize %d, selecting level %d with cell size %f\n",
			boxW,boxH,tileSize,lvl_,WebMercatorCellSizesf[lvl_]);

	static constexpr int permOrder[MAX_LVLS] = { 10,11,12,13,14,15,16,17,18, 9,8,7,6,5,4, 19,20,21,22, 3,2,1,0, 23,24,25 };

	// (2)
	uint64_t lvl = lvl_;
	int step = 1;
	while (dbs[lvl] == INVALID_DB) {
		lvl += step;
		if (lvl == MAX_LVLS) { lvl = lvl_-1; step = -1; }
		if (lvl == -1) {
			printf(" - Failed to find valid db for lvl (originally selected %d)\n", lvl_);
		}
	}
	if (lvl_ != lvl) printf(" - Originally picked lvl %d, but not index there, so used lvl %lu\n", lvl_, lvl);

	// (3)


	return lvl;
}

uint64_t DatasetReader::findBestLvlAndTlbr_dataDependent(uint64_t tlbr[4], int imgH, int imgW, const double bboxWm[4], MDB_txn* txn) {

	// 1. Find optimal level
	// 2. Find closest level that exists in entire db
	// 3. Find level that actually covers box
	//
	// Note: Step 2 is merely an optimization for step 3. It could be skipped.
	// Note: Step 3 only checks the top-left and bottomo-right corners, and assumes
	//       all interior tiles exist, without actually checking.

	// (1)
	double boxW = bboxWm[2] - bboxWm[0];
	double boxH = bboxWm[3] - bboxWm[1];
	float mean = (boxW + boxH) * .5f; // geometric mean makes more sense really.
	float x = (mean / static_cast<float>(std::min(imgH,imgW))) * (tileSize);
	int lvl_ = floor_log2_i_(WebMercatorCellSizesf[0] / x);
	printf(" - Given bboxWm %lfw %lfh, tileSize %d, selecting level %d with cell size %f\n",
			boxW,boxH,tileSize,lvl_,WebMercatorCellSizesf[lvl_]);

	//static constexpr int permOrder[MAX_LVLS] = { 10,11,12,13,14,15,16,17,18, 9,8,7,6,5,4, 19,20,21,22, 3,2,1,0, 23,24,25 };

	// (2)
	uint64_t lvl = lvl_;
	int step = 1;
	while (dbs[lvl] == INVALID_DB) {
		lvl += step;
		if (lvl == MAX_LVLS) { lvl = lvl_-1; step = -1; }
		if (lvl == -1) {
			printf(" - Failed to find valid db for lvl (originally selected %d)\n", lvl_);
		}
	}
	if (lvl_ != lvl) printf(" - Originally picked lvl %d, but not index there, so used lvl %lu\n", lvl_, lvl);
	lvl_ = lvl;

	// (3)
	bool good = false;
	while (not good) {
		double s = (.5 * (1<<lvl)) / WebMercatorScale;

#if 0
		tlbr[0] = static_cast<uint64_t>((WebMercatorScale + bboxWm[0]) * s);
		tlbr[1] = static_cast<uint64_t>((WebMercatorScale + bboxWm[1]) * s);
		tlbr[2] = static_cast<uint64_t>(std::ceil((WebMercatorScale + bboxWm[2]) * s));
		tlbr[3] = static_cast<uint64_t>(std::ceil((WebMercatorScale + bboxWm[3]) * s));
#else
		uint64_t w = 1 + std::max(std::ceil((WebMercatorScale + bboxWm[2]) * s) - (WebMercatorScale + bboxWm[0]) * s,
							  std::ceil((WebMercatorScale + bboxWm[3]) * s) - (WebMercatorScale + bboxWm[1]) * s);
		tlbr[0] = static_cast<uint64_t>((WebMercatorScale + bboxWm[0]) * s);
		tlbr[1] = static_cast<uint64_t>((WebMercatorScale + bboxWm[1]) * s);
		tlbr[2] = w + static_cast<uint64_t>((WebMercatorScale + bboxWm[0]) * s);
		tlbr[3] = w + static_cast<uint64_t>((WebMercatorScale + bboxWm[1]) * s);
#endif

		good = tileExists(BlockCoordinate{lvl,tlbr[1],tlbr[0]},txn) and
			   tileExists(BlockCoordinate{lvl,tlbr[3],tlbr[2]},txn);

		printf(" - does tile %luz %luy %lux exist? -> %s\n", lvl,tlbr[1],tlbr[0], tileExists(BlockCoordinate{lvl,tlbr[1],tlbr[0]},txn) ? "yes" : "no");
		printf(" - does tile %luz %luy %lux exist? -> %s\n", lvl,tlbr[3],tlbr[2], tileExists(BlockCoordinate{lvl,tlbr[3],tlbr[2]},txn) ? "yes" : "no");

		if (not good)
			lvl--;

		if (lvl <= 0) {
			printf(" - (findBestLvlAndTlbr) picked lvl %d, but searched all the way down to lvl 0 and could not find tiles to cover it.\n", lvl_);
			throw std::runtime_error(std::string{"(findBestLvlAndTlbr) failed to find level with tiles covering it"});
		}
	}



	return lvl;
}
