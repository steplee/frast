#include "db.h"
#include "image.h"
#include "utils/solve.hpp"
#include "utils/memcpy_utils.hpp"

#include <cassert>
#include <string>
#include <unordered_set>
#include <stack>

#include <sys/stat.h>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <array>
#include <iomanip>
#include <cmath>

#include <fmt/color.h>
#include <fmt/core.h>

//#define DEBUG_RASTERIO
#ifdef DEBUG_RASTERIO
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#endif

/*
 * LMDB observations:
 *		- Using a single txn for many writes is MUCH faster
 *		- Using a single txn for many reads is marginally faster, if at all.
 */

AtomicTimer t_total("total"),
			t_encodeImage("encodeImage"), t_decodeImage("decodeImage"), t_mergeImage("mergeImage"),
			t_rasterIo("rasterIo"), t_fetchBlocks("fetchBlocks"), t_warp("warp"), t_memcpyStrided("memcpyStrided"),
			t_getCached("getCached"), t_solve("solve"), t_gdal("gdal"),
			t_dbWrite("dbWrite"), t_dbRead("dbRead"), t_dbBeginTxn("dbBeginTxn"), t_dbEndTxn("dbEndTxn"), t_tileBufferCopy("tileBufferCopy");
void printDebugTimes() {}

static int myIntCompare(const MDB_val *a, const MDB_val *b) {
	assert(a->mv_size == 8);
	assert(b->mv_size == 8);
	uint64_t aa = *(const uint64_t*)a->mv_data;
	uint64_t bb = *(const uint64_t*)b->mv_data;
	if (aa < bb) return -1;
	if (aa == bb) return 0;
	return 1;
}


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
	allowInflate = dopts.allowInflate;

	for (int i=0; i<MAX_LVLS; i++) dbs[i] = INVALID_DB;

	if (auto err = mdb_env_create(&env)) {
		throw std::runtime_error(std::string{"mdb_env_create failed with "} + mdb_strerror(err));
	}

	mdb_env_set_mapsize(env, dopts.mapSize);
	printf(" - Setting mapSize (initial  ) to %lu\n", dopts.mapSize);

	mdb_env_set_maxdbs(env, MAX_LVLS+1);

	int flags = MDB_NOSUBDIR;
	if (readOnly) flags |= MDB_RDONLY;
	if (not dopts.threadLocalStorage) flags |= MDB_NOTLS;
	// See if this speeds up writes.
	//if (not readOnly) flags |= MDB_NORDAHEAD;
	mode_t fileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
	if (auto err = mdb_env_open(env, path.c_str(), flags, fileMode)) {
		throw std::runtime_error(std::string{"mdb_env_open failed with "} + mdb_strerror(err));
	}

	memset(&meta, 0, sizeof(DatasetMeta));
	meta.fixedSizeMeta.mapSize = dopts.mapSize;

	open_all_dbs();

}

bool Dataset::beginTxn(MDB_txn** txn, bool readOnly) const {
	AtomicTimerMeasurement g(t_dbBeginTxn);
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

	//int txn_flags = MDB_RDONLY;
	int txn_flags = readOnly ? MDB_RDONLY : 0;
	if (auto err = mdb_txn_begin(env, nullptr, txn_flags, &txn)) {
		throw std::runtime_error(std::string{"(open_all_dbs) mdb_txn_begin failed with "} + mdb_strerror(err));
	}

	{
		int flags = 0;
		flags = MDB_CREATE;
		if (auto err = mdb_dbi_open(txn, "meta", flags, &metaDb))
			std::cout << " - (open_all_dbs) failed to open metaDb \n";
		decode_meta_(txn);
	}

	mdb_env_set_mapsize(env, meta.fixedSizeMeta.mapSize);
	printf(" - Setting mapSize (from meta) to %lu\n", meta.fixedSizeMeta.mapSize);

	for (int i=0; i<MAX_LVLS; i++) {
		std::string name = std::string{"lvl"} + std::to_string(i);
		int flags = 0;
		if (auto err = mdb_dbi_open(txn, name.c_str(), flags, dbs+i))
			//throw std::runtime_error(std::string{"(open_all_dbs) mdb_dbi_open failed with "} + mdb_strerror(err));
			//std::cout << " - (open_all_dbs) failed to open lvl " << i << "\n";
			{}
		else {

			mdb_set_compare(txn, dbs[i], &myIntCompare);
			mdb_set_dupsort(txn, dbs[i], &myIntCompare);
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
	mdb_set_compare(txn, dbs[lvl], &myIntCompare);
	mdb_set_dupsort(txn, dbs[lvl], &myIntCompare);
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

		// Try to inflate before failing.
		if (allowInflate and !getInflate_(out, coord, theTxn)) {
			// inflate success
			if (givenTxn == nullptr) endTxn(&theTxn);
			return false;
		}

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
	//std::cout << " - writing keylen " << key.mv_size << " vallen " << val.mv_size << " to lvl " << coord.z() << "\n";
	auto err = mdb_put(txn, dbs[coord.z()], &key, &val, allowOverwrite ? 0 : MDB_NOOVERWRITE);
	return err;
}

bool Dataset::getInflate_(Image& out, const BlockCoordinate& coord, MDB_txn* txn) {
	uint64_t z = coord.z();
	uint64_t y = coord.y();
	uint64_t x = coord.x();

	bool found_ancestor = false;
	MDB_val key { 8, (void*)(&coord.c) };
	MDB_val eimg_;
	BlockCoordinate bc(0);

	while (z > 0) {
		z--;
		y >>= 1;
		x >>= 1;
		bc = BlockCoordinate { z , y , x };

		if (dbs[z]) {
			if (not get_(eimg_, bc, txn)) {
				found_ancestor = true;
				break;
			}
		}
	}

	if (not found_ancestor) {
		printf(" - getInflate (%lu %lu %lu) no ancestor found.\n",
				coord.z(), coord.y(), coord.x());
		return true;
	}

	// TODO: Move this to a class member to avoid allocs
	Image tmp { out.h, out.w, out.format };
	tmp.alloc();

	// Decode.
	{
		EncodedImageRef eimg { eimg_.mv_size, (uint8_t*) eimg_.mv_data };
		AtomicTimerMeasurement g(t_decodeImage);
		bool ret = decode(tmp, eimg);
		if (ret) return true;
	}

	// Sample the correct part of the image.
	{
		uint32_t lvlOffset = coord.z() - z;
		uint32_t div = 1 << lvlOffset;
		uint64_t y_off = div - 1 - (coord.y() % div);
		uint64_t x_off = (coord.x() % div);
		float y_off_pix = static_cast<float>(y_off * tileSize()) / div;
		float x_off_pix = static_cast<float>(x_off * tileSize()) / div;

		/*
		float grid[8] = {
			static_cast<float>((x_off+0) * tileSize()) / div,
			static_cast<float>((y_off+0) * tileSize()) / div,

			static_cast<float>((x_off+1) * tileSize()) / div,
			static_cast<float>((y_off+0) * tileSize()) / div,

			static_cast<float>((x_off+1) * tileSize()) / div,
			static_cast<float>((y_off+1) * tileSize()) / div,

			static_cast<float>((x_off+0) * tileSize()) / div,
			static_cast<float>((y_off+1) * tileSize()) / div,
		};
		printf(" - getInflate (%lu %lu %lu) -> (%lu %lu %lu) grid [%f %f -> %f %f] (%u %u)\n",
				coord.z(), coord.y(), coord.x(), z,y,x, grid[0], grid[1], grid[6], grid[7], lvlOffset, div);
		tmp.remapRemap(out, grid, 2, 2);
		*/

		float H[9] = {
			//1.f / div, 0, (float)x_off / div,
			//0, 1.f / div, (float)y_off / div,
			(float)div, 0, -(float)x_off_pix * div,
			0, (float)div, -(float)y_off_pix * div,
			0, 0, 1.f
		};
		tmp.warpAffine(out, H);
	}

	return false;
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
		return err;
		//assert(false);
	}

	else if (err == MDB_MAP_FULL and givenTxn != nullptr) {

		// Below approach does not work: the old db gets erase on mdb_env_set_mapsize.
		// So you need to have two envs open and copy old into resized new.
		// But for now, I'll just assume the env mapSize is always large enough.
		throw std::runtime_error("DB grew too large! Must increase size!");
		/*

		// Commit, then transparently create new transaction, and re-run
		mdb_txn_commit(*givenTxn);

		meta.fixedSizeMeta.mapSize <<= 1;
		printf(" - Map was full, doubling size to %lu\n", meta.fixedSizeMeta.mapSize);
		mdb_env_set_mapsize(env, meta.fixedSizeMeta.mapSize);

		//std::cout << " - (put) [silently commiting and recreating input txn]\n";
		beginTxn(givenTxn);
		theTxn = *givenTxn;
		err = put_(val, coord, theTxn, allowOverwrite);
		if (err) std::cout << " - mdb_put error (after map resize): " << mdb_strerror(err) << "\n";
		*/
	} else if (err)
		printf(" - mdb_put error: %s, (key %lu %lu %lu) (val %p, %zu)\n", mdb_strerror(err),
				coord.z(), coord.y(), coord.x(), val.mv_data, val.mv_size);
	else
		//printf(" - mdb_put success: (key %lu %lu %lu) (val %p, %zu)\n", coord.z(), coord.y(), coord.x(), val.mv_data, val.mv_size);

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

	if (nn > 0) {
		tlbr[2] += 1;
		tlbr[3] += 1;
	}

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
	if (lvl < 0 or lvl > MAX_LVLS-1) return false;
	if (dbs[lvl] == INVALID_DB) return false;
	MDB_val key, val;
	key.mv_data = (void*) &(bc.c);
	key.mv_size = sizeof(BlockCoordinate);
	if (auto err = mdb_get(txn, dbs[lvl], &key, &val)) {
		if (err == MDB_NOTFOUND) return false;
		else throw std::runtime_error(std::string{"(tileExists " + std::to_string(lvl) + ") mdb err "} + mdb_strerror(err));
	}
	return true;
}

bool Dataset::hasLevel(int lvl) const {
	return dbs[lvl] != INVALID_DB;
}

void Dataset::decode_meta_(MDB_txn* txn) {


	uint64_t zero = 0, one = 1;
	MDB_val key1 { 8, &zero };
	MDB_val val1;
	MDB_val key2 { 8, &one };
	MDB_val val2;
	if (auto err = mdb_get(txn, metaDb, &key1, &val1)) {
		printf(" - decode_meta_ failed.\n");
	} else {
		assert(val1.mv_size == sizeof(DatasetMeta::FixedSizeMeta));
		memcpy(&meta.fixedSizeMeta, val1.mv_data, val1.mv_size);

		if (auto err = mdb_get(txn, metaDb, &key2, &val2)) {
		} else {
			meta.regions.resize(val2.mv_size / sizeof(DatasetMeta::Region));
			memcpy(meta.regions.data(), val2.mv_data, val2.mv_size);
		}

		dprintf(" - file has %zu regions, c=%d, tileSize=%d.\n", meta.regions.size(), channels(), tileSize());
	}
	for (int i=0; i<MAX_LVLS; i++) {
		if (meta.fixedSizeMeta.levelMetas[i].nTiles > 0)
			dprintf(" - (meta) lvl %2d : %6d tiles [%7d %7d -> %7d %7d]\n",
					i, meta.fixedSizeMeta.levelMetas[i].nTiles,
					meta.fixedSizeMeta.levelMetas[i].tlbr[0], meta.fixedSizeMeta.levelMetas[i].tlbr[1], meta.fixedSizeMeta.levelMetas[i].tlbr[2], meta.fixedSizeMeta.levelMetas[i].tlbr[3]);
	}
}
void Dataset::recompute_meta_and_write_slow(MDB_txn* txn) {
	// This function fills in two datas
	//      1) The global regions
	//      2) Each levels' tlbr
	//
	// My way of filling regions is to find connected components.
	// But tiles are only considered if they have no parents.
	//
	// The level tlbr is simpler.
	// Both done at the same time.
	//
	// TODO: set mapSize to a smaller but safe value.

	std::vector<DatasetMeta::Region> regions;

	std::unordered_set<uint64_t> lastLvl;
	for (int lvl=MAX_LVLS-1; lvl>=0; lvl--) {
		std::unordered_set<uint64_t> curLvl;
		std::unordered_set<uint64_t> curLvlCopy;
		uint64_t lvlTlbr[4] = { (uint64_t)9e19, (uint64_t)9e19, 0, 0 };
		//double curRegion[4] = { 9e19, 9e19, -9e19, -9e19 };
		uint64_t curRegion[4] = { (uint64_t)9e19, (uint64_t)9e19, 0, 0 };
		DatasetMeta::LevelMeta newLevelMeta = {{(uint64_t)9e19,(uint64_t)9e19,0,0}, 0};
		if (not hasLevel(lvl)) {
			newLevelMeta.nTiles = 0;
			meta.fixedSizeMeta.levelMetas[lvl] = newLevelMeta;
		} else {
			iterLevel(lvl, txn, [&](const BlockCoordinate& coord, MDB_val& val) {
				curLvl.insert(coord.c);
				if (coord.x() < newLevelMeta.tlbr[0]) newLevelMeta.tlbr[0] = coord.x();
				if (coord.y() < newLevelMeta.tlbr[1]) newLevelMeta.tlbr[1] = coord.y();
				if (coord.x() > newLevelMeta.tlbr[2]) newLevelMeta.tlbr[2] = coord.x();
				if (coord.y() > newLevelMeta.tlbr[3]) newLevelMeta.tlbr[3] = coord.y();
			});

			newLevelMeta.nTiles = curLvl.size();
			meta.fixedSizeMeta.levelMetas[lvl] = newLevelMeta;

			// Below, we erase from the map. But we need to save it to
			// move it to lastLvl for the next iter. So make a copy here.
			curLvlCopy = curLvl;
			uint64_t ulvl = (uint64_t) lvl;

			// Find connected components
			while (not curLvl.empty()) {
				std::stack<decltype(curLvl)::iterator> st;
				st.push(curLvl.begin());
				int nInRegion = 0;
				std::unordered_set<uint64_t> seen;
				seen.insert(*curLvl.begin());
				while (not st.empty()) {
					auto it = st.top();
					st.pop();
					BlockCoordinate coord = *it;
					//seen.insert(*it);
					curLvl.erase(it);
					bool hasParent =
						lastLvl.find(BlockCoordinate{ulvl+1, coord.y()*2+0, coord.x()*2+0}.c) != lastLvl.end() or
						lastLvl.find(BlockCoordinate{ulvl+1, coord.y()*2+1, coord.x()*2+0}.c) != lastLvl.end() or
						lastLvl.find(BlockCoordinate{ulvl+1, coord.y()*2+1, coord.x()*2+1}.c) != lastLvl.end() or
						lastLvl.find(BlockCoordinate{ulvl+1, coord.y()*2+0, coord.x()*2+1}.c) != lastLvl.end();
					if (hasParent) {
						// Has parent, so this is not a valid tile to expand
						//printf(" - Tile %luz %luy %lux has parent, stopping.\n", coord.z(),coord.y(),coord.x());
					} else {
						if (coord.x() < curRegion[0]) curRegion[0] = coord.x();
						if (coord.y() < curRegion[1]) curRegion[1] = coord.y();
						if (coord.x() > curRegion[2]) curRegion[2] = coord.x();
						if (coord.y() > curRegion[3]) curRegion[3] = coord.y();
						nInRegion++;
						//printf(" - Growing with tile %luz %luy %lux.\n", coord.z(),coord.y(),coord.x());

						for (int j=0; j<4; j++) {
							int dy = j == 0 ? -1 : j == 1 ? 1 : 0;
							int dx = j == 2 ? -1 : j == 3 ? 1 : 0;
							BlockCoordinate neighCoord { ulvl, coord.y()+dy, coord.x()+dx };
							auto neigh = curLvl.find(neighCoord.c);
							if (neigh != curLvl.end() and seen.find(neighCoord.c) == seen.end()) {
								seen.insert(neighCoord.c);
								st.push(neigh);
							}
						}
					}
				}

				// We've exhausted a connected component.
				// If curRegion is valid, it is a new one!
				if (nInRegion > 0) {
					regions.push_back({
							curRegion[0] * WebMercatorMapScale * 2. / (1<<ulvl) - WebMercatorMapScale,
							curRegion[1] * WebMercatorMapScale * 2. / (1<<ulvl) - WebMercatorMapScale,
							curRegion[2] * WebMercatorMapScale * 2. / (1<<ulvl) - WebMercatorMapScale,
							curRegion[3] * WebMercatorMapScale * 2. / (1<<ulvl) - WebMercatorMapScale });
					printf(" - Found region (lvl %d) (%d tiles) (%lf %lf -> %lf %lf)\n", lvl,
							nInRegion, regions.back().tlbr[0],regions.back().tlbr[1], regions.back().tlbr[2], regions.back().tlbr[3]);

					curRegion[0] = (uint64_t) 9e19;
					curRegion[1] = (uint64_t) 9e19;
					curRegion[2] = (uint64_t) 0;
					curRegion[3] = (uint64_t) 0;
				}
			}
		}
		lastLvl = curLvlCopy;
	}

	dprintf(" - Found %d regions.\n", regions.size());
	// Put fixed size level tlbrs
	// Put variable sized regions
	uint64_t zero = 0, one = 1;
	MDB_val key1 { 8, &zero };
	MDB_val key2 { 8, &one };
	MDB_val val1 { sizeof(meta.fixedSizeMeta), (void*) &meta.fixedSizeMeta };
	MDB_val val2 { sizeof(DatasetMeta::Region)*regions.size(), (void*)regions.data() };
	if (auto err = mdb_put(txn, metaDb, &key1, &val1, 0))
		throw std::runtime_error("mdb_put error");
	if (auto err = mdb_put(txn, metaDb, &key2, &val2, 0))
		throw std::runtime_error("mdb_put error");

	meta.regions = std::move(regions);
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
	//image.copyFrom(tile.image);
	if (tile.eimg.size() > eimg.size()) {
		eimg.resize(tile.eimg.size() * 2);
		std::copy(tile.eimg.begin(), tile.eimg.end(), eimg.begin());
	}
	coord = tile.coord;
}
void WritableTile::fillWith(const BlockCoordinate& c, const MDB_val& val) {
	AtomicTimerMeasurement g(t_tileBufferCopy);
	if (val.mv_size > eimg.size()) {
		eimg.resize(val.mv_size * 2);
	}
	std::copy((uint8_t*)val.mv_data, ((uint8_t*)val.mv_data)+val.mv_size, eimg.begin());
	coord = c;
}


DatasetWritable::~DatasetWritable() {
	doStop = true;
	w_cv.notify_one();
	if (w_thread.joinable()) w_thread.join();
	dprintf (" - (~DatasetWritable join w_thread)\n");
}

#if 0
void DatasetWritable::w_loop() {
	//beginTxn(&w_txn, false);

	while (true) {
		int nWaitingCommands = 0;
		std::vector<Command> commands;
		//usleep(20'000);
		{
			std::unique_lock<std::mutex> lck(w_mtx);
			
			w_cv.wait(lck, [this]{return doStop or pushedCommands.size();});
			nWaitingCommands = pushedCommands.size();
			while (pushedCommands.size()) {
				commands.push_back(Command{});
				pushedCommands.pop_front(commands.back());
			}

			if (doStop and nWaitingCommands == 0) {
				dprintf(" - (wthread exiting.)\n");
				break;
			}
			if (!doStop and nWaitingCommands == 0) {
				dprintf(" - (wthread, spurious wakeup.\n");
			}
		}

		std::sort(commands.begin(), commands.end(), [](const Command& a, const Command& b) {
				if (a.cmd == Command::BeginLvl) return true;
				if (a.cmd == Command::EndLvl) return true;
				if (a.cmd == Command::EraseLvl) return true;
				if (b.cmd == Command::BeginLvl) return false;
				if (b.cmd == Command::EndLvl) return false;
				if (b.cmd == Command::EraseLvl) return false;
				return a.data.tileBufferIdx < b.data.tileBufferIdx;
		});

		//printf(" - (awoke to %d avail) handling push of tile %d\n", nAvailable, theTileIdx); fflush(stdout);
		//if (commands.size() > 1) printf(" - (awoke to %d avail)\n", commands.size()); fflush(stdout);

		for (auto &theCommand : commands) {
			if (theCommand.cmd != Command::NoCommand) {

				if (theCommand.cmd == Command::BeginLvl) {
					std::unique_lock<std::mutex> lck(w_mtx);
					printf(" - recv command to start lvl %d\n", theCommand.data.lvl); fflush(stdout);
					if (w_txn) endTxn(&w_txn);
					this->createLevelIfNeeded(theCommand.data.lvl);
					beginTxn(&w_txn, false);
					nWaitingCommands--;
					curTransactionWriteCount = 0;

					for (int i=0; i<numWorkers; i++) {
						tileBufferIdxMtx[i].lock();
						tileBufferCommittedIdx[i] = 0;
						tileBufferLendedIdx[i] = 0;
						tileBufferIdxMtx[i].unlock();
					}
				} else if (theCommand.cmd == Command::EndLvl) {
					std::unique_lock<std::mutex> lck(w_mtx);
					printf(" - recv command to end lvl %d, with %d other cmds\n", theCommand.data.lvl, commands.size()); fflush(stdout);

					// TODO XXX Messy spagghetti code while i test this out...
					// Finish all remaining images.
					for (int i=0; i<numWorkers; i++) {
						tileBufferIdxMtx[i].lock();
						while (tileBufferCommittedIdx[i] < tileBufferLendedIdx[i]) {
							int theTileIdx = i * buffersPerWorker + ((tileBufferCommittedIdx[i]) % buffersPerWorker);
							tileBufferCommittedIdx[i]++;
							WritableTile& tile = tileBuffers[theTileIdx];
							this->put(tile.eimg.data(), tile.eimg.size(), tile.coord, &w_txn, false);
							curTransactionWriteCount++;
						}
						tileBufferIdxMtx[i].unlock();
					}


					assert(w_txn);
					endTxn(&w_txn);
					//beginTxn(&w_txn, false);
					nWaitingCommands--;
					printf(" - recv command to end lvl %d ... done\n", theCommand.data.lvl); fflush(stdout);
				} else if (theCommand.cmd == Command::EraseLvl) {
					std::unique_lock<std::mutex> lck(w_mtx);
					dprintf(" - recv command to erase lvl %d\n", theCommand.data.lvl); fflush(stdout);
					if (w_txn) {
						printf(" - Cannot have open w_txn while erasing level. Should sent 'EndLvl' first.\n"); fflush(stdout);
						exit(1);
					}
					beginTxn(&w_txn, false);
					dropLvl(theCommand.data.lvl, w_txn);
					endTxn(&w_txn);
					nWaitingCommands--;
				}





				/*
				if (theCommand.cmd == Command::TileReady) {
					int theTileIdx = theCommand.data.tileBufferIdx;
					int theWorker = theTileIdx / buffersPerWorker;
					//printf(" - recv command to commit tilebuf %d (worker %d, buf %d), nWaiting: %d\n", theCommand.data.tileBufferIdx, theWorker, theTileIdx, nWaitingCommands); fflush(stdout);
					WritableTile& tile = tileBuffers[theTileIdx];
					this->put(tile.eimg.data(), tile.eimg.size(), tile.coord, &w_txn, false);
					curTransactionWriteCount++;

					tileBufferIdxMtx[theWorker].lock();
					tileBufferCommittedIdx[theWorker] += 1;
					tileBufferIdxMtx[theWorker].unlock();
				}
				*/

				// Only commit a full set. That helps the in-order-ness
				int setSize = buffersPerWorker / 2;
				if (theCommand.cmd == Command::TileReady or theCommand.cmd == Command::TileReadyOverwrite) {
					int theTileIdx = theCommand.data.tileBufferIdx;
					int theWorker = theTileIdx / buffersPerWorker;
					//printf(" - recv command to commit tilebuf %d (worker %d, buf %d), nWaiting: %d\n", theCommand.data.tileBufferIdx, theWorker, theTileIdx, nWaitingCommands); fflush(stdout);

					if (theTileIdx % setSize == setSize - 1) {
						//printf(" - committing range ");
						for (int j=setSize-1; j>=0; j--) {
							int theTileIdx_ = (theTileIdx - j);
							if (theTileIdx_ < 0) theTileIdx_ = buffersPerWorker + theTileIdx_;
							//printf("%d ",theTileIdx_);
							WritableTile& tile = tileBuffers[theTileIdx_];
							this->put(tile.eimg.data(), tile.eimg.size(), tile.coord, &w_txn, theCommand.cmd == Command::TileReadyOverwrite);
							curTransactionWriteCount++;
						}
						//printf("\n");

						tileBufferIdxMtx[theWorker].lock();
						tileBufferCommittedIdx[theWorker] += setSize;
						tileBufferIdxMtx[theWorker].unlock();
					}
					theCommand.cmd = Command::NoCommand;
				}
				nWaitingCommands--;
			}
		}

		// Force a write.
		// prevents mdb_spill_page from dominating cycles.
		if (curTransactionWriteCount >= maxTransactionWriteCount) {
			//std::unique_lock<std::mutex> lck(w_mtx);
			printf(" - Ending large (%d) transaction, worker threads may block!\n", curTransactionWriteCount);

			endTxn(&w_txn);
			beginTxn(&w_txn, false);
			curTransactionWriteCount = 0;
		}


		//if (doStop or nWaitingCommands > 0) w_cv.notify_one();
	}

	//if (w_txn) endTxn(&w_txn);
}
#else
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
				dprintf(" - (wthread exiting.)\n");
				break;
			}
			if (!doStop and nWaitingCommands == 0) {
				dprintf(" - (wthread, spurious wakeup.\n");
			}


			// Lock the mutex if creating or ending a level.
			// The TileReady command needn't hold mutex.
			if (theCommand.cmd == Command::BeginLvl) {
				dprintf(" - recv command to start lvl %d\n", theCommand.data.lvl); fflush(stdout);
				if (w_txn) endTxn(&w_txn);
				this->createLevelIfNeeded(theCommand.data.lvl);
				beginTxn(&w_txn, false);
				nWaitingCommands--;
				curTransactionWriteCount = 0;
			} else if (theCommand.cmd == Command::EndLvl) {
				dprintf(" - recv command to end lvl %d\n", theCommand.data.lvl); fflush(stdout);
				assert(w_txn);
				endTxn(&w_txn);
				//beginTxn(&w_txn, false);
				nWaitingCommands--;
			} else if (theCommand.cmd == Command::EraseLvl) {
				dprintf(" - recv command to erase lvl %d\n", theCommand.data.lvl); fflush(stdout);
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
			if (theCommand.cmd == Command::TileReady or theCommand.cmd == Command::TileReadyOverwrite) {
				int theTileIdx = theCommand.data.tileBufferIdx;
				int theWorker = theTileIdx / buffersPerWorker;
				//printf(" - recv command to commit tilebuf %d (worker %d, buf %d), nWaiting: %d\n", theCommand.data.tileBufferIdx, theWorker, theTileIdx, nWaitingCommands); fflush(stdout);
				WritableTile& tile = tileBuffers[theTileIdx];
				this->put(tile.eimg.data(), tile.eimg.size(), tile.coord, &w_txn, theCommand.cmd == Command::TileReadyOverwrite);
				curTransactionWriteCount++;

				tileBufferIdxMtx[theWorker].lock();
				tileBufferCommittedIdx[theWorker] += 1;
				tileBufferIdxMtx[theWorker].unlock();
			}
			nWaitingCommands--;
		}

		// Force a write.
		// prevents mdb_spill_page from dominating cycles.
		if (curTransactionWriteCount >= maxTransactionWriteCount) {
			//std::unique_lock<std::mutex> lck(w_mtx);
			printf(" - Ending large (%d) transaction, worker threads may block!\n", curTransactionWriteCount);

			endTxn(&w_txn);
			beginTxn(&w_txn, false);
			curTransactionWriteCount = 0;
		}


		if (doStop or nWaitingCommands > 0) w_cv.notify_one();
	}

	//if (w_txn) endTxn(&w_txn);
}
#endif

// Note: this ASSUMES the correct thread is calling the func.
WritableTile& DatasetWritable::blockingGetTileBufferForThread(int thread) {
	int nwaited = 0;

	uint32_t sleepTime = 2'500;
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

			nwaited++;
			if (thread == 0 and (nwaited == 20 or nwaited == 50 or nwaited == 100 or nwaited == 250 or nwaited % 500 == 0))
				printf(" - too many buffers lent [thr %d] (cmt %d, lnd %d, nbuf %d), waited %d times...\n",
						thread, comtIdx, lendIdx, buffersPerWorker, nwaited);
			usleep(sleepTime);
			if (sleepTime < 150'000) sleepTime += sleepTime/2;
		}
	}
}

void DatasetWritable::configure(int numWorkerThreads, int buffersPerWorker) {
	assert(numWorkerThreads <= MAX_THREADS);
	this->numWorkers = numWorkerThreads;
	this->buffersPerWorker = buffersPerWorker;

	nBuffers = numWorkers * buffersPerWorker;
	tileBuffers.resize(nBuffers);
	for (int i=0; i<numWorkerThreads; i++) {
		tileBufferLendedIdx[i] = 0;
		tileBufferCommittedIdx[i] = 0;
	}
	for (int i=0; i<nBuffers; i++) {
		//tileBuffers[i].image = Image { tileSize(), tileSize(), channels() };
		//tileBuffers[i].image.calloc();
		tileBuffers[i].bufferIdx = i;
		//printf(" - made buffer tile with idx %d\n", tileBuffers[i].bufferIdx);
	}

	for (int i=0; i<numWorkerThreads; i++) {
		perThreadTileCache[i] = LruCache<uint64_t, Image>(WRITER_CACHE_CAPACITY);
	}

	pushedCommands = RingBuffer<Command>(nBuffers + 16);
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
			usleep(10'000);
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

	uint32_t sleepTime = 5'000;
	int steps = 0;
	while (not empty) {
		usleep(sleepTime);
		{
			std::lock_guard<std::mutex> lck(w_mtx);
			empty = pushedCommands.empty();
		}
		if (sleepTime < 50'000) sleepTime += sleepTime / 2;
		steps++;
		if (steps % 10 == 0) printf(" - blockUntilEmptiedQueue, waited %d times\n", steps);
	}
	//printf(" - blockUntilEmptiedQueue :: size at end: %d\n", pushedCommands.size());
}

bool DatasetWritable::getCached(int tid, Image& out, const BlockCoordinate& coord, MDB_txn** txn) {
	if (tid < 0 or tid >= MAX_THREADS) {
		dprintf(" - (getCached) bad tid, not looking at cache", coord.z(), coord.y(), coord.x());
		return get(out,coord,txn);
	}

	auto& tileCache = perThreadTileCache[tid];
	if (tileCache.get(out, coord.c)) {
		//printf(" - (getCached) [thr %d] cache hit for tile %luz %luy %lux\n", tid, coord.z(), coord.y(), coord.x());
		return false;
	}
	if (get(out,coord,txn)) {
		// Failed. Do not cache.
		return true;
	}
	//printf(" - (getCached) [thr %d] cache miss for tile %luz %luy %lux\n", tid, coord.z(), coord.y(), coord.x());
	tileCache.set(coord.c, out);
	return false;
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

	accessCache1 = Image {                      tileSize(),                      tileSize(), format() };
	accessCache  = Image { dopts.maxSampleTiles*tileSize(), dopts.maxSampleTiles*tileSize(), format() };
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
	AtomicTimerMeasurement g(t_rasterIo);

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

	double edgeLen;
	{
		// Heron's formula. This is a bit overkill.
		auto length_ = [](float x1, float y1, float x2, float y2) { return sqrtf((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1)); };
		/*
		float a = length_(quad[0*2+0], quad[0*2+1], quad[1*2+0], quad[1*2+1]);
		float b = length_(quad[0*2+0], quad[0*2+1], quad[2*2+0], quad[2*2+1]);
		float c = length_(quad[0*2+0], quad[0*2+1], quad[3*2+0], quad[3*2+1]);
		float s = (a+b+c) / 2.f;
		edgeLen = sqrtf(2.f * sqrtf(s * (s-a) * (s-b) * (s-c)));
		*/
		edgeLen = .5 * (
			length_(quad[0*2+0], quad[0*2+1], quad[1*2+0], quad[1*2+1]) +
			length_(quad[1*2+0], quad[1*2+1], quad[3*2+0], quad[3*2+1]));
	}
	dprintf(" - Edge Len: %f with ow oh %d %d\n", edgeLen, ow, oh);
	uint64_t tileTlbr[4];
	uint64_t lvl = findBestLvlAndTlbr_dataDependent(tileTlbr, accessCache.capacity, oh,ow, edgeLen, bboxWm, r_txn_);
	if (lvl == BAD_LEVEL_CHOSEN) {
		return true;
	}
	double s = (.5 * (1<<lvl)) / WebMercatorMapScale;

	/*
	double sampledTlbr[4] = {
		std::floor((WebMercatorMapScale + bboxWm[0]) * s) / s - WebMercatorMapScale,
		std::floor((WebMercatorMapScale + bboxWm[1]) * s) / s - WebMercatorMapScale,
		std::ceil((WebMercatorMapScale + bboxWm[2]) * s) / s - WebMercatorMapScale,
		std::ceil((WebMercatorMapScale + bboxWm[3]) * s) / s  - WebMercatorMapScale};
	*/
	double sampledTlbr[4] = {
		((double)tileTlbr[0]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale,
		((double)tileTlbr[1]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale,
		((double)tileTlbr[2]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale,
		((double)tileTlbr[3]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale };

	int ny = tileTlbr[3] - tileTlbr[1], nx = tileTlbr[2] - tileTlbr[0];
	// sampled width/height
	int sw = nx*tileSize(), sh = ny*tileSize();
	dprintf(" - (rasterIoQuad) sampling [%d %d tiles] [%d %d px] [lvl %2d] to fill outputBuffer of size [%d %d px]\n",
			ny,nx, sh,sw, lvl, oh,ow);

	// If >2 tiles, and enabled, use bg threads
	// In either case, after this, accessCache will have the needed tiles.
	fetchBlocks(accessCache, lvl, tileTlbr, r_txn_);

	if (auto err = endTxn(&r_txn_, true))
		throw std::runtime_error(std::string{"(findBestLvlAndTlbr) mdb_txn_end failed with "} + mdb_strerror(err));

	// Find the perspective transformation that takes the sampled image into the
	// queried quad.
	// printf(" - sampledTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			// sampledTlbr[0], sampledTlbr[1], sampledTlbr[2], sampledTlbr[3],
			// sampledTlbr[2] - sampledTlbr[0], sampledTlbr[3] - sampledTlbr[1]);
	// printf(" - queryTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			// bboxWm[0], bboxWm[1], bboxWm[2], bboxWm[3], bboxWm[2] - bboxWm[0], bboxWm[3] - bboxWm[1]);

	float x_scale = sw / (sampledTlbr[2] - sampledTlbr[0]);
	float y_scale = sh / (sampledTlbr[3] - sampledTlbr[1]);
	alignas(16) float in_corners[8] = {
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
	alignas(16) float out_corners[8] = {
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
	for (int y=0; y<ny; y++) dbgImg1(cv::Rect(0, y*tileSize(), sw, 1)) = cv::Scalar{0};
	for (int x=0; x<nx; x++) dbgImg1(cv::Rect(x*tileSize(), 0, 1, sh)) = cv::Scalar{0};
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

	alignas(16) float H[9];
	//float *H;
	{
		AtomicTimerMeasurement g(t_solve);
		/*
		cv::Mat a { 4, 2, CV_32F,  in_corners };
		cv::Mat b { 4, 2, CV_32F, out_corners };
		cv::Mat h = cv::getPerspectiveTransform(a,b);
		//cv::Mat h = cv::getPerspectiveTransform(b,a);
		if (h.type() == CV_64F) h.convertTo(h, CV_32F);
		float *H = (float*) h.data;
		*/
		// My Eigen-based solve is 2x faster than cv one.
		solveHomography(H, in_corners, out_corners);
	}
	/*printf(" - Got H:\n");
	for (int y=0;y<3;y++) {
	for (int x=0;x<3;x++)
		printf("%f ", H[y*3+x]);
		printf("\n");
	}*/

	// printf(" - in_corners:\n %f %f\n %f %f\n %f %f\n %f %f\n",
			// in_corners[0], in_corners[1],
			// in_corners[2], in_corners[3],
			// in_corners[4], in_corners[5],
			// in_corners[6], in_corners[7]);
	// printf(" - out_corners:\n %f %f\n %f %f\n %f %f\n %f %f\n",
			// out_corners[0], out_corners[1],
			// out_corners[2], out_corners[3],
			// out_corners[4], out_corners[5],
			// out_corners[6], out_corners[7]);

	// Warp affine must get correct sample w/h
	int push_w = accessCache.w, push_h = accessCache.h;
	accessCache.w = sw;
	accessCache.h = sh;
	// printf(" - Warping %d %d %d -> %d %d %d\n",
			// accessCache.w, accessCache.h, accessCache.channels(),
			// out.w, out.h, out.channels());
	{
		AtomicTimerMeasurement g(t_warp);
		accessCache.warpPerspective(out, H);
	}
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
	uint64_t lvl = findBestLvlAndTlbr_dataDependent(tileTlbr, accessCache.capacity, oh,ow, bboxWm, r_txn_);
	if (lvl == BAD_LEVEL_CHOSEN) {
		auto err = endTxn(&r_txn_, true);
		return true;
	}

	double s = (.5 * (1<<lvl)) / WebMercatorMapScale;


	// double sampledTlbr[4] = {
		// std::floor(bboxWm[0] * s) / s,
		// std::floor(bboxWm[1] * s) / s,
		// std::ceil(bboxWm[2] * s) / s,
		// std::ceil(bboxWm[3] * s) / s };
	double sampledTlbr[4] = {
		((double)tileTlbr[0]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale,
		((double)tileTlbr[1]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale,
		((double)tileTlbr[2]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale,
		((double)tileTlbr[3]) * WebMercatorMapScale * 2. / (1<<lvl) - WebMercatorMapScale };

	int ny = tileTlbr[3] - tileTlbr[1], nx = tileTlbr[2] - tileTlbr[0];
	// sampled width/height
	int sw = nx*tileSize(), sh = ny*tileSize();
	dprintf(" - (rasterIo) sampling [%d %d tiles] [%d %d px] to fill outputBuffer of size [%d %d px]\n",
			ny,nx, sh,sw, oh,ow);

	// If >2 tiles, and enabled, use bg threads
	// In either case, after this, accessCache will have the needed tiles.
	fetchBlocks(accessCache, lvl, tileTlbr, r_txn_);

	if (auto err = endTxn(&r_txn_, true))
		throw std::runtime_error(std::string{"(findBestLvlAndTlbr) mdb_txn_end failed with "} + mdb_strerror(err));

	// Find the affine transformation that takes the sampled image into the
	// queried bbox.
	// printf(" - sampledTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			// sampledTlbr[0], sampledTlbr[1], sampledTlbr[2], sampledTlbr[3],
			// sampledTlbr[2] - sampledTlbr[0], sampledTlbr[3] - sampledTlbr[1]);
	// printf(" - queryTlbr %lf %lf | %lf %lf (%lf %lf)\n",
			// bboxWm[0], bboxWm[1], bboxWm[2], bboxWm[3], bboxWm[2] - bboxWm[0], bboxWm[3] - bboxWm[1]);

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
	for (int y=0; y<ny; y++) dbgImg1(cv::Rect(0, y*tileSize(), sw, 1)) = cv::Scalar{0};
	for (int x=0; x<nx; x++) dbgImg1(cv::Rect(x*tileSize(), 0, 1, sh)) = cv::Scalar{0};
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
	cv::Mat dbgImg2 = cv::Mat(out.h, out.w, accessCache.channels()==3?CV_8UC3:CV_8UC1, out.buffer).clone();
	cv::imshow("warped", dbgImg2);
	cv::waitKey(1);
#endif

	// Warp affine must get correct sample w/h
	int push_w = accessCache.w, push_h = accessCache.h;
	accessCache.w = sw;
	accessCache.h = sh;
	// printf(" - Warping %d %d %d -> %d %d %d\n",
			// accessCache.w, accessCache.h, accessCache.channels(),
			// out.w, out.h, out.channels());
	{
		AtomicTimerMeasurement g(t_warp);
		accessCache.warpAffine(out, A);
	}
	accessCache.w = push_w;
	accessCache.h = push_h;


	return false;
}

bool DatasetReader::getCached(Image& out, const BlockCoordinate& coord, MDB_txn** txn) {
	AtomicTimerMeasurement g(t_getCached);
	if (opts.nthreads > 1) tileCacheMtx.lock();
	{
		if (tileCache.get(out, coord.c)) {
			//printf(" - (getCached) cache hit for tile %luz %luy %lux\n", coord.z(), coord.y(), coord.x());
			if (opts.nthreads > 1) tileCacheMtx.unlock();
			return false;
		}
	}

	if (opts.nthreads > 1) tileCacheMtx.unlock();
	if (get(out,coord,txn)) {
		// Failed. Do not cache.
		return true;
	}
	//printf(" - (getCached) cache miss for tile %luz %luy %lux\n", coord.z(), coord.y(), coord.x());
	if (opts.nthreads > 1) tileCacheMtx.lock();
	{
		tileCache.set(coord.c, out);
	}
	if (opts.nthreads > 1) tileCacheMtx.unlock();
	return false;
}

// Returns number of invalid tiles.
// Note: the w and h of 'out' may be changed by function! (But capacity will not, and should be high enough)
int DatasetReader::fetchBlocks(Image& out, uint64_t lvl, const uint64_t tlbr[4], MDB_txn* txn0) {
	AtomicTimerMeasurement g(t_fetchBlocks);

	if (tlbr[0] == fetchedCacheBox[0] and
	    tlbr[1] == fetchedCacheBox[1] and
	    tlbr[2] == fetchedCacheBox[2] and
	    tlbr[3] == fetchedCacheBox[3]) {
		//AtomicTimerMeasurement g(t_encodeImage);
		//printf(" - (fetchBlocks) cache hit copying (%d %d %d : %d) to (%d %d %d : %d).\n", fetchedCache.w, fetchedCache.h, fetchedCache.channels(), fetchedCache.size(),
				// out.w, out.h, out.channels(), out.size()); fflush(stdout);
		out = fetchedCache;
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
	//assert(out.w >= tileSize()*nx);
	//assert(out.h >= tileSize()*ny);
	int sw = nx*tileSize(); // Sampled width and height
	int sh = ny*tileSize();
	if (out.capacity < sw*sh*channels()) {
		char buf[256];
		sprintf(buf, "output buffer (%d %d) was to small for fetchBlocks (sampled %d %d)", out.w, out.h, sw, sh);
		throw std::runtime_error(std::string{buf});
		//throw std::runtime_error("output buffer was too small for fetchBlocks (sw,sh " + std::to_string(sw) + "," + std::to_string(sh) + ")");
	}
	out.w = sw;
	out.h = sh;

	int nMissing = 0;

	// If >2 tiles, and enabled, use bg threads
	if (opts.nthreads == 0 or nx*ny <= 2) {
		for (int yi=0; yi<ny; yi++) {
		for (int xi=0; xi<nx; xi++) {
			BlockCoordinate tileCoord { lvl, tlbr[1]+yi, tlbr[0]+xi };
			if (getCached(accessCache1, tileCoord, &txn0)) {
				dprintf(" - Failed to get tile %lu %lu %lu\n", tileCoord.z(), tileCoord.y(), tileCoord.x());
				memset(accessCache1.buffer, 0, tileSize()*tileSize()*channels());
				nMissing++;
			}

			if (out.format == Image::Format::TERRAIN_2x8) {
				uint16_t* dst = reinterpret_cast<uint16_t*>(out.buffer) + (ny-1-yi)*(tileSize()*sw*channels()) + xi*(tileSize()*channels());
				const uint16_t* src = reinterpret_cast<const uint16_t*>(accessCache1.buffer);
				if (channels() == 1)      memcpyStridedOutputFlatInput<uint16_t,1>(dst, src, sw, tileSize(), tileSize());
				else if (channels() == 3) memcpyStridedOutputFlatInput<uint16_t,3>(dst, src, sw, tileSize(), tileSize());
				else if (channels() == 4) memcpyStridedOutputFlatInput<uint16_t,4>(dst, src, sw, tileSize(), tileSize());
			} else {
				uint8_t* dst = (out.buffer) + (ny-1-yi)*(tileSize()*sw*out.channels()) + xi*(tileSize()*out.channels());
				if (out.channels() == 4 and channels() == 1) memcpyStridedOutputFlatInputReplicateRgbPadAlpha<uint8_t>(dst, accessCache1.buffer, sw, tileSize(), tileSize());
				else if (out.channels() == 4 and channels() == 3) memcpyStridedOutputFlatInputPadAlpha<uint8_t>(dst, accessCache1.buffer, sw, tileSize(), tileSize());
				else if (channels() == 1) memcpyStridedOutputFlatInput<uint8_t,1>(dst, accessCache1.buffer, sw, tileSize(), tileSize());
				else if (channels() == 3) memcpyStridedOutputFlatInput<uint8_t,3>(dst, accessCache1.buffer, sw, tileSize(), tileSize());
				else if (channels() == 4) memcpyStridedOutputFlatInput<uint8_t,4>(dst, accessCache1.buffer, sw, tileSize(), tileSize());
			}

		}
		}
	} else {
		// TODO: Multi-threaded load.
		// ...
		assert(false);
	}

	if (ownTxn)
		if (auto err = endTxn(&txn0))
			throw std::runtime_error(std::string{"(fetchBlocks) mdb_txn_end failed with "} + mdb_strerror(err));

	// Populate cache.
	//printf(" - setting fetched cache (%d %d, out %d %d).\n", fetchedCache.h, fetchedCache.w, out.h, out.w);
	{
	//AtomicTimerMeasurement g(t_encodeImage);
	fetchedCache = out;
	}
	fetchedCacheMissing = nMissing;
	fetchedCacheBox[0] = tlbr[0];
	fetchedCacheBox[1] = tlbr[1];
	fetchedCacheBox[2] = tlbr[2];
	fetchedCacheBox[3] = tlbr[3];

	return nMissing;
}


static constexpr int BAD_FLOOR_VALUE = -10;
static int floor_log2_i_(float x) {
	// assert(x >= 0);
	if (x <= 0) return BAD_FLOOR_VALUE;

	// Could also use floating point log2.
	// Could also convert x to an int and use intrinsics.
	int i = 0;
	int xi = x * 4.f; // offset by 2^2 to get better resolution, if x < 1.

	// Bad quality
	//while ((1<<(i+1)) < xi) { i++; };
	// Medium quality
	while ((1<<i) < xi) { i++; };
	// Good quality
	//i=1; while ((1<<(i-1)) < xi) { i++; };
	return i-2;
}

/*
uint64_t DatasetReader::findBestLvlForBoxAndRes(int imgH, int imgW, const double bboxWm[4]) {
	assert(false); // deprecated

	// 1. Find optimal level
	// 2. Find closest level that exists in entire db

	// (1)
	double boxW = bboxWm[2] - bboxWm[0];
	double boxH = bboxWm[3] - bboxWm[1];
	float mean = (boxW + boxH) * .5f; // geometric mean makes more sense really.
	float x = (mean / static_cast<float>(std::min(imgH,imgW))) * (tileSize());
	int lvl_ = floor_log2_i_(WebMercatorCellSizesf[0] / x);
	// printf(" - Given bboxWm %lfw %lfh, tileSize() %d, selecting level %d with cell size %f\n",
			// boxW,boxH,tileSize(),lvl_,WebMercatorCellSizesf[lvl_]);

	static constexpr int permOrder[MAX_LVLS] = { 10,11,12,13,14,15,16,17,18, 9,8,7,6,5,4, 19,20,21,22, 3,2,1,0, 23,24,25 };

	// (2)
	uint64_t lvl = lvl_;
	int step = 1;
	while (dbs[lvl] == INVALID_DB) {
		lvl += step;
		if (lvl == MAX_LVLS) { lvl = lvl_-1; step = -1; }
		if (lvl == -1) {
			dprintf(" - Failed to find valid db for lvl (originally selected %d)\n", lvl_);
		}
	}
	if (lvl_ != lvl) dprintf(" - Originally picked lvl %d, but not index there, so used lvl %lu\n", lvl_, lvl);

	// (3)


	return lvl;
}
*/

uint64_t DatasetReader::findBestLvlAndTlbr_dataDependent(uint64_t tlbr[4], uint32_t outCapacity, int imgH, int imgW, const double bboxWm[4], MDB_txn* txn) {
	double boxW = bboxWm[2] - bboxWm[0];
	double boxH = bboxWm[3] - bboxWm[1];
	float meanEdgeLen = (boxW + boxH) * .5f; // geometric mean makes more sense really.
	return findBestLvlAndTlbr_dataDependent(tlbr, outCapacity, imgH,imgW, meanEdgeLen, bboxWm, txn);
}
uint64_t DatasetReader::findBestLvlAndTlbr_dataDependent(uint64_t tlbr[4], uint32_t outCapacity, int imgH, int imgW, float edgeLen, const double bboxWm[4], MDB_txn* txn) {

	// 1. Find optimal level
	// 2. Find closest level that exists in entire db
	// 3. Find level that actually covers box
	//
	// Note: Step 2 is merely an optimization for step 3. It could be skipped.
	// Note: Step 3 only checks the top-left and bottomo-right corners, and assumes
	//       all interior tiles exist, without actually checking.

	// (1)
	float x = (edgeLen / static_cast<float>(std::min(imgH,imgW))) * (tileSize());
	int lvl_ = floor_log2_i_(WebMercatorCellSizesf[0] / x);
	if (lvl_ == BAD_FLOOR_VALUE) {
		printf(" - BAD FLOOR VALUE : Given bboxWm %lfw %lfh, edge %f, tileSize() %d, selecting level %d\n", bboxWm[2]-bboxWm[0],bboxWm[3]-bboxWm[1],edgeLen,tileSize(),lvl_);
		// fmt::print(fmt::fg(fmt::color::red), " - BAD FLOOR VALUE : Given bboxWm {}w {}h, edge {}, tileSize() {}, selecting level {}\n", bboxWm[2]-bboxWm[0],bboxWm[3]-bboxWm[1],edgeLen,tileSize(),lvl_);
		return BAD_LEVEL_CHOSEN;
	}
	//printf(" - Given bboxWm %lfw %lfh, tileSize() %d, selecting level %d with cell size %f\n",
			//bboxWm[2]-bboxWm[0],bboxWm[3]-bboxWm[1],tileSize(),lvl_,WebMercatorCellSizesf[lvl_]);

	//static constexpr int permOrder[MAX_LVLS] = { 10,11,12,13,14,15,16,17,18, 9,8,7,6,5,4, 19,20,21,22, 3,2,1,0, 23,24,25 };

	// (2)
	uint64_t lvl = lvl_;
	int step = 1;
	while (dbs[lvl] == INVALID_DB) {
		lvl += step;
		if (lvl == MAX_LVLS) { lvl = lvl_-1; step = -1; }
		if (lvl == -1) {
			dprintf(" - Failed to find valid db for lvl (originally selected %d)\n", lvl_);
		}
	}
	if (lvl_ != lvl) dprintf(" - Originally picked lvl %d, but not index there, so used lvl %lu\n", lvl_, lvl);
	lvl_ = lvl;

	// (3)
	bool good = false;
	while (not good) {
		double s = (.5 * (1<<lvl)) / WebMercatorMapScale;

#if 1
		tlbr[0] = static_cast<uint64_t>((WebMercatorMapScale + bboxWm[0]) * s);
		tlbr[1] = static_cast<uint64_t>((WebMercatorMapScale + bboxWm[1]) * s);
		tlbr[2] = static_cast<uint64_t>(std::ceil((WebMercatorMapScale + bboxWm[2]) * s));
		tlbr[3] = static_cast<uint64_t>(std::ceil((WebMercatorMapScale + bboxWm[3]) * s));
		//tlbr[2] = static_cast<uint64_t>(std::floor((WebMercatorMapScale + bboxWm[2]) * s));
		//tlbr[3] = static_cast<uint64_t>(std::floor((WebMercatorMapScale + bboxWm[3]) * s));
#else
		uint64_t w = 1 + std::max(std::ceil((WebMercatorMapScale + bboxWm[2]) * s) - (WebMercatorMapScale + bboxWm[0]) * s,
							  std::ceil((WebMercatorMapScale + bboxWm[3]) * s) - (WebMercatorMapScale + bboxWm[1]) * s);
		tlbr[0] = static_cast<uint64_t>((WebMercatorMapScale + bboxWm[0]) * s);
		tlbr[1] = static_cast<uint64_t>((WebMercatorMapScale + bboxWm[1]) * s);
		tlbr[2] = w + static_cast<uint64_t>((WebMercatorMapScale + bboxWm[0]) * s);
		tlbr[3] = w + static_cast<uint64_t>((WebMercatorMapScale + bboxWm[1]) * s);
#endif

		int32_t nx = tlbr[2] - tlbr[0];
		int32_t ny = tlbr[3] - tlbr[1];
		int sw = nx*tileSize(); // Sampled width and height
		int sh = ny*tileSize();

		good = tileExists(BlockCoordinate{lvl,tlbr[1],tlbr[0]},txn) and
			   tileExists(BlockCoordinate{lvl,tlbr[3]-1,tlbr[2]-1},txn) and
			   outCapacity >= sw*sh*channels();
		dprintf(" - Testing capacity %d vs %d\n", outCapacity, sw*sh*channels());

		//printf(" - does tile %luz %luy %lux exist? -> %s\n", lvl,tlbr[1],tlbr[0], tileExists(BlockCoordinate{lvl,tlbr[1],tlbr[0]},txn) ? "yes" : "no");
		//printf(" - does tile %luz %luy %lux exist? -> %s\n", lvl,tlbr[3],tlbr[2], tileExists(BlockCoordinate{lvl,tlbr[3],tlbr[2]},txn) ? "yes" : "no");

		if (not good) {
			lvl--;
			//printf(" - (findBestLvlAndTlbr_dataDependent) missing tile on lvl %d, going down one to %d\n", lvl+1,lvl);
		}

		if (lvl <= 0 or lvl >= 99) {
			printf(" - (findBestLvlAndTlbr) picked lvl %d, but searched all the way down to lvl 0 and could not find tiles to cover it.\n", lvl_);
			//throw std::runtime_error(std::string{"(findBestLvlAndTlbr) failed to find level with tiles covering it"});
			return BAD_LEVEL_CHOSEN;
		}
	}



	return lvl;
}


DatasetMeta::Region DatasetMeta::computeCoveredRegion() const {
	if (regions.size() == 0) return Region{{0,0,0,0}};
	Region region { {9e19,9e19, -9e19,-9e19} };
	for (auto& r : regions) {
		if (r.tlbr[0] < region.tlbr[0]) region.tlbr[0] = r.tlbr[0];
		if (r.tlbr[1] < region.tlbr[1]) region.tlbr[1] = r.tlbr[1];
		if (r.tlbr[2] > region.tlbr[2]) region.tlbr[2] = r.tlbr[2];
		if (r.tlbr[3] > region.tlbr[3]) region.tlbr[3] = r.tlbr[3];
	}
	return region;
}
