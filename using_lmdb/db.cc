#include "db.h"
#include "image.h"

#include <lmdb.h>
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

double _encodeTime = 0;
double _decodeTime = 0;
double _imgMergeTime = 0;
double _dbWriteTime = 0;
double _dbReadTime = 0;
double _dbEndTxnTime = 0;
double _totalTime = 0;


Dataset::Dataset(OpenMode m, const std::string& path, const DatabaseOptions& dopts)
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

bool Dataset::beginTxn(MDB_txn** txn, bool readOnly) {
	int flags = readOnly ? MDB_RDONLY : 0;
	auto err = mdb_txn_begin(env, nullptr, flags, txn);
	if (err)
		throw std::runtime_error(std::string{"(beginTxn) mdb_txn_begin failed with "} + mdb_strerror(err));
	return err != 0;
}
bool Dataset::endTxn(MDB_txn** txn, bool abort) {
	AddTimeGuard g(_dbEndTxnTime);
	if (abort)
		mdb_txn_abort(*txn);
	else
		mdb_txn_commit(*txn);
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
			std::cout << " - (open_all_dbs) failed to open lvl " << i << "\n";
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
		AddTimeGuard g(_decodeTime);
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
		auto err = mdb_put(txn, dbs[coord.z()], &key, &val, allowOverwrite ? 0 : MDB_NOOVERWRITE);
		return err;
	}
}

bool Dataset::put(Image& in,  const BlockCoordinate& coord, MDB_txn** givenTxn, bool allowOverwrite) {
	MDB_txn* theTxn;

	if (givenTxn == nullptr) {
		if (auto err = mdb_txn_begin(env, nullptr, 0, &theTxn))
			throw std::runtime_error(std::string{"(put) mdb_txn_begin failed with "} + mdb_strerror(err));
	} else
		theTxn = *givenTxn;


	// XXX NOTE:
	// If the key is already present and we must merge, this function encodes the image the first time for no reason.
	// If you expect many conflicts (for example, merging two tiffs of overlapping aoi), then this function
	// should mdb_get to see if the key exists and avoid the first encode!

	MDB_val val;
	EncodedImage eimg;
	{
		AddTimeGuard g(_encodeTime);

		if (0) {
		Image in2 { in.w, in.h, in.format };
		in2.alloc();
		float H[6] = { 1,-.1,1, .05,1,2 };
		in.warpAffine(in2, H);
		encode(eimg, in2);
		} else
		encode(eimg, in);

		val = MDB_val{ eimg.size(), static_cast<void*>(eimg.data()) };
	}

	auto err = put_(val, coord, theTxn, allowOverwrite);


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

	else if (err == MDB_MAP_FULL and givenTxn != nullptr) {
		throw std::runtime_error("DB grew too large! Must increase size!");
		// Commit, then transparently create new transaction, and re-run
		//mdb_txn_commit(*givenTxn);
		//std::cout << " - (put) [silently commiting and recreating input txn]\n";
		//beginTxn(givenTxn);
		//theTxn = *givenTxn;
		//err = put_(val, coord, theTxn);
	} else if (err) std::cout << " - mdb_put error: " << mdb_strerror(err) << "\n";

	//if (givenTxn == nullptr) mdb_txn_commit(theTxn);
	if (givenTxn == nullptr) endTxn(&theTxn);
	return err != 0;
}


bool Dataset::rasterio(Image& out, double aoiUwm[4]) const {
	return true;
}


