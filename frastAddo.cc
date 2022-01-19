#include "db.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <omp.h>

#include <unordered_set>


/*
 *
 * Program to add and delete overviews on a given dataset.
 *
 * My original code require the input to have tiles on just one level.
 * However that is undesriable when 'frastMerge`ing datasets of diffrent
 * resolutions.
 *
 * So, I added the safeMakeOverviews and safeRemoveOverviews functions.
 *
 * There is no good way to remove overviews from multi-resolution tiles,
 * unless you keep a bit for each tile as having been created as an overview or not.
 * That's because sometimes you need to delete when it has only all four children,
 * or only one.
 * Hopefully it is not a big issue.
 *
 * TODO:
 * I copied the code for original to safeMakeOverviews.
 * It finds the AABB and walks through each tile.
 * This is awful for merged datasets.
 * Instead, for every level, search for previous parents.
 * If there is >0, make the child.
 *
 */

#define ADDO_THREADS 4
static_assert(ADDO_THREADS < DatasetWritable::MAX_THREADS);

namespace {
	bool encode_cv__(EncodedImage& out, const cv::Mat& mat) {
		AddTimeGuardAsync g(_encodeTime);
		cv::imencode(".jpg", mat, out);
		return false;
	}

std::vector<int> findExistingLvls(DatasetWritable& dset) {
	std::vector<int> out;
	dset.getExistingLevels(out);
	return out;
}


[[deprecated]]
int removeOverviews(DatasetWritable& dset, const std::vector<int>& existingLvls) {
	int baseLvl = existingLvls[0];

	for (int i=0; i<existingLvls.size()-1; i++) {
		printf(" - (sending cmd to erase lvl %d)\n", existingLvls[i]);
		dset.sendCommand(Command{Command::EraseLvl, existingLvls[i]});
	}

	return 0;
}

/*
 *
 * For each level [in descending order]:
 * Only delete tiles that have 4 children.
 *
 * NOTE XXX: There is no way to do this correctly unless I add a bit flag to the stored data
 *           that marks it as an overview.
 *           Because a parent tile is created from possible <4 children, we have two options here:
 *               1) Delete the tile only if it has exactly 4 children
 *               2) Delete the tile if it has any number of children.
 *           Until I add that 'is_overview' flag, there is no correct solution for datasets
 *           that were merged from different levels.
 *
 *           ~~~For now I will go with option (1), which means we'll have lingering tiles
 *           that were created from overviews. That's probably fine though.~~~
 *
 *           *Actually* No, I'll go with (2). Just beware of overlapping datasets merged from different resolutions...
 *
 * The algorithm will propagate deletions down the tree properly, as long as
 * each parent has the full four children when creating overviews.
 *
 */
int safeRemoveOverviews(DatasetWritable& dset, const std::vector<int>& existingLvls) {
	int baseLvl = existingLvls[0];

	std::unordered_set<uint64_t> nxtLvlTiles;

	for (int lvli=existingLvls.size()-1; lvli>=0; lvli--) {
		int lvl = existingLvls[lvli];
		printf(" - Sweeping lvl %d\n", lvl);

		MDB_txn* txn;
		if (dset.beginTxn(&txn)) throw std::runtime_error("Failed to open txn.");

		std::unordered_set<uint64_t> curLvlTiles;
		std::unordered_set<uint64_t> theseTilesHaveParents;
		std::unordered_set<uint64_t> theseTilesCanBeErased;
		int nDeleted = 0;

		dset.iterLevel(lvl, txn, [&curLvlTiles, &nxtLvlTiles, &theseTilesHaveParents](const BlockCoordinate& bc, MDB_val& val) {
				//BlockCoordinate parentCoord { bc.z() - 1, bc.y() >> 1, bc.x() >> 1 };
				curLvlTiles.insert(static_cast<const uint64_t>(bc));
		});
		printf(" - Lvl %d, %d tiles\n", lvl, curLvlTiles.size());


		int childrenHisto[4] = {0};
		for (uint64_t k : curLvlTiles) {
			BlockCoordinate parent { k };
			BlockCoordinate child1 { parent.z()+1, parent.y()*2+0, parent.x()*2+0 };
			BlockCoordinate child2 { parent.z()+1, parent.y()*2+1, parent.x()*2+0 };
			BlockCoordinate child3 { parent.z()+1, parent.y()*2+1, parent.x()*2+1 };
			BlockCoordinate child4 { parent.z()+1, parent.y()*2+0, parent.x()*2+1 };
			int nChildren = 0;
			if (nxtLvlTiles.find(child1) != nxtLvlTiles.end()) nChildren++;
			if (nxtLvlTiles.find(child2) != nxtLvlTiles.end()) nChildren++;
			if (nxtLvlTiles.find(child3) != nxtLvlTiles.end()) nChildren++;
			if (nxtLvlTiles.find(child4) != nxtLvlTiles.end()) nChildren++;

			// See function comment about 1/2
			//if (nChildren == 4) {
			if (nChildren > 0) {
				// We can erase the parent.
				// Note: keep them in the curLvlTiles map, so that deletions propagate down the tree.
				bool stat = dset.erase(parent, txn);
				if (stat) {
					printf(" - Failed to erase parent tile %luz $luy %lux\n", parent.z(),parent.y(),parent.x());
					return 1;
				}
				nDeleted++;
			}
			childrenHisto[nChildren]++;
		}

		printf(" - Lvl %d, children histogram:\n", lvl);
		for (int i=0; i<4; i++) printf("     %i: %6d\n",i,childrenHisto[i]);

		int nLeft = curLvlTiles.size() - nDeleted;
		printf(" - nLeft = %d (%d - %d)\n", nLeft, curLvlTiles.size(), nDeleted);
		if (nLeft == 0) {
			printf(" - Level %d had no tiles left, erasing it.\n", lvl);
			if (dset.endTxn(&txn)) throw std::runtime_error("Failed to close txn.");
			if (dset.beginTxn(&txn)) throw std::runtime_error("Failed to open txn.");
			dset.dropLvl(lvl,txn);
			if (dset.endTxn(&txn)) throw std::runtime_error("Failed to open txn.");
		} else if (dset.endTxn(&txn)) throw std::runtime_error("Failed to close txn.");

		nxtLvlTiles = std::move(curLvlTiles);

		//printf(" - (sending cmd to erase lvl %d)\n", existingLvls[i]);
		//dset.sendCommand(Command{Command::EraseLvl, existingLvls[i]});
	}

	return 0;
}

[[deprecated]]
int makeOverviews(DatasetWritable& dset, const std::vector<int>& existingLvls) {
	assert(existingLvls.size() == 1);

	int baseLvl = existingLvls[0];
	uint64_t lvlTlbr[4];
	int64_t n = dset.determineLevelAABB(lvlTlbr, baseLvl);

	for (int i=0; i<4; i++) lvlTlbr[i] >>= 1lu;
	int64_t nrows = lvlTlbr[2] - lvlTlbr[0];
	int64_t ncols = lvlTlbr[3] - lvlTlbr[1];

	int channels = 1;
	int cv_type = channels == 3 ? CV_8UC3 : channels == 4 ? CV_8UC4 : CV_8U;
	int tileSize = 256;

	Image tmpImage_[ADDO_THREADS];
	cv::Mat tmpMat_[ADDO_THREADS];
	cv::Mat parent_[ADDO_THREADS];
	cv::Mat child_[ADDO_THREADS];
	MDB_txn** r_txn_[ADDO_THREADS];
	EncodedImage eimg_[ADDO_THREADS];

	for (int i=0; i<ADDO_THREADS; i++) {
		tmpImage_[i] = Image { tileSize, tileSize, channels };
		tmpImage_[i].alloc();
		tmpMat_[i]   = cv::Mat ( tileSize, tileSize, cv_type, tmpImage_[i].buffer );
		parent_[i]   = cv::Mat ( tileSize*2, tileSize*2, cv_type );
		child_[i]    = cv::Mat ( tileSize  , tileSize  , cv_type );
		r_txn_[i]    = nullptr;
	}

	/*
	Image tmpImage { tileSize, tileSize, channels };
	tmpImage.alloc();
	cv::Mat tmpMat ( tileSize, tileSize, cv_type, tmpImage.buffer );
	cv::Mat parent 
	MDB_txn** r_txn = nullptr;
	EncodedImage eimg;
	*/


#pragma omp parallel num_threads(ADDO_THREADS) shared(lvlTlbr)
	for (uint64_t lvl=baseLvl-1; ; lvl--) {

		{
		int tid = omp_get_thread_num();
		if (tid == 0)
			dset.sendCommand(Command{Command::BeginLvl, (int32_t) lvl});
		}

#pragma omp for schedule(static)
		for (uint64_t y=lvlTlbr[1]; y<lvlTlbr[3]; y++) {

			int tid = omp_get_thread_num();
			Image& tmpImage = tmpImage_[tid];
			cv::Mat& tmpMat = tmpMat_[tid];
			cv::Mat& parent = parent_[tid];
			cv::Mat& child = child_[tid];
			MDB_txn**& r_txn = r_txn_[tid];
			EncodedImage& eimg = eimg_[tid];

			for (uint64_t x=lvlTlbr[0]; x<lvlTlbr[2]; x++) {

				// Get four parents
				int nMissingParents = 0;
				//printf(" - making tile %luz %luy %lux\n", lvl,y,x);
				for (uint64_t j=0; j<4; j++) {
					BlockCoordinate pcoord { lvl+1, y*2 + j/2, x*2 + j%2 };
					if (dset.get(tmpImage, pcoord, r_txn)) {
						// Tile didn't exist, just make it black.
						memset(tmpImage.buffer, 0, channels*tileSize*tileSize);
						nMissingParents++;
					}
					tmpMat.copyTo(parent(cv::Rect{tileSize*(((int32_t)j)/2), tileSize*(((int32_t)j)%2), tileSize, tileSize}));
				}

				if (nMissingParents == 4) {
					printf(" - Strange: tile %luz %luy %lux was missing all parents? Skipping it.\n", lvl,y,x);
				} else {
					printf(" - [thr %d] making tile %lu %lu %lu, with %d parents\n", tid, lvl,y,x, 4-nMissingParents);

					// Downsample
					cv::resize(parent, child, cv::Size{tileSize,tileSize});

					// Put
					WritableTile& wtile = dset.blockingGetTileBufferForThread(tid);
					wtile.coord = BlockCoordinate { lvl, y, x };
					//dset.push(wtile);
					dset.sendCommand(Command{Command::TileReady, wtile.bufferIdx});
				}
			}
		}

#pragma omp barrier

		int tid = omp_get_thread_num();
		//printf(" - post loop, tid: %d\n", tid);
		if (tid == 0) {
			dset.sendCommand(Command{Command::EndLvl, (int32_t) lvl});

			for (int i=0; i<4; i++) lvlTlbr[i] >>= 1;
			nrows = lvlTlbr[2] - lvlTlbr[0];
			ncols = lvlTlbr[3] - lvlTlbr[1];
		}
#pragma omp barrier
		if (nrows <= 1 or ncols <= 1) {
			if (tid == 0) printf(" - Stopping Addo on lvl %lu (nrows %lu, ncols %lu)\n", lvl, nrows, ncols);
			break;
		}

		// Note: It is necessary to synchronize with the w_thread before moving onto next level,
		//       since we read parent tiles from it.
		if (tid == 0)
			while (dset.hasOpenWrite()) {
				usleep(10'000);
			}
#pragma omp barrier
	}

	return 0;
}

int safeMakeOverviews(DatasetWritable& dset, const std::vector<int>& existingLvls) {
	int baseLvl = existingLvls.back();
	uint64_t lvlTlbr[4];
	int64_t n = dset.determineLevelAABB(lvlTlbr, baseLvl);

	for (int i=0; i<4; i++) lvlTlbr[i] >>= 1lu;
	int64_t nrows = lvlTlbr[2] - lvlTlbr[0];
	int64_t ncols = lvlTlbr[3] - lvlTlbr[1];

	int channels = 1;
	int cv_type = channels == 3 ? CV_8UC3 : channels == 4 ? CV_8UC4 : CV_8U;
	int tileSize = 256;

	Image tmpImage_[ADDO_THREADS];
	cv::Mat tmpMat_[ADDO_THREADS];
	cv::Mat parent_[ADDO_THREADS];
	cv::Mat child_[ADDO_THREADS];
	//MDB_txn* r_txn_[ADDO_THREADS];
	EncodedImage eimg_[ADDO_THREADS];

	for (int i=0; i<ADDO_THREADS; i++) {
		tmpImage_[i] = Image { tileSize, tileSize, channels };
		tmpImage_[i].alloc();
		tmpMat_[i]   = cv::Mat ( tileSize, tileSize, cv_type, tmpImage_[i].buffer );
		parent_[i]   = cv::Mat ( tileSize*2, tileSize*2, cv_type );
		child_[i]    = cv::Mat ( tileSize  , tileSize  , cv_type );
		//r_txn_[i]    = nullptr;
	}

#pragma omp parallel num_threads(ADDO_THREADS) shared(lvlTlbr)
	for (uint64_t lvl=baseLvl-1; ; lvl--) {

		{
			int tid = omp_get_thread_num();
			if (tid == 0) {
				dset.sendCommand(Command{Command::BeginLvl, (int32_t) lvl});
				dset.blockUntilEmptiedQueue();
			}
		}
#pragma omp barrier
		int tid_ = omp_get_thread_num();
		MDB_txn* r_txn = nullptr;
		if (dset.beginTxn(&r_txn, true)) {
			printf(" - beginTxn failed.\n"); fflush(stdout);
			throw std::runtime_error("beginTxn failed");
		}
#pragma omp barrier

#pragma omp for schedule(static)
		for (uint64_t y=lvlTlbr[1]; y<lvlTlbr[3]; y++) {

			int tid = omp_get_thread_num();
			Image& tmpImage = tmpImage_[tid];
			cv::Mat& tmpMat = tmpMat_[tid];
			cv::Mat& parent = parent_[tid];
			cv::Mat& child = child_[tid];
			EncodedImage& eimg = eimg_[tid];


			for (uint64_t x=lvlTlbr[0]; x<lvlTlbr[2]; x++) {
				BlockCoordinate myCoord { lvl, y, x };
				//printf(" - [thr %d] checking if existing tile %luz %luy %lux with txn %p\n", tid, lvl,y,x, r_txn);

				if (dset.tileExists(myCoord, r_txn)) {
					// Skip.
					// TODO: Actually: should merge with downsampled version.
					printf(" - [thr %d] skipping existing tile %luz %luy %lux\n", tid, lvl,y,x);
					continue;
				}

				// Get four parents
				int nMissingParents = 0;
				//printf(" - making tile %luz %luy %lux\n", lvl,y,x);
				for (uint64_t j=0; j<4; j++) {
					BlockCoordinate pcoord { lvl+1, y*2 + j/2, x*2 + j%2 };
					if (dset.get(tmpImage, pcoord, &r_txn)) {
						// Tile didn't exist, just make it black.
						memset(tmpImage.buffer, 0, channels*tileSize*tileSize);
						nMissingParents++;
					}
					tmpMat.copyTo(parent(cv::Rect{tileSize*(((int32_t)j)/2), tileSize*(((int32_t)j)%2), tileSize, tileSize}));
				}

				if (nMissingParents == 4) {
					//printf(" - [thr %d] Strange: tile %luz %luy %lux was missing all parents? Skipping it.\n", tid, lvl,y,x);
				} else {
					//printf(" - [thr %d] making tile %lu %lu %lu, with %d parents\n", tid, lvl,y,x, 4-nMissingParents);

					// Downsample
					cv::resize(parent, child, cv::Size{tileSize,tileSize});

					// Put
					WritableTile& wtile = dset.blockingGetTileBufferForThread(tid);
					encode_cv__(wtile.eimg, child);
					wtile.coord = myCoord;
					//dset.push(wtile);
					dset.sendCommand(Command{Command::TileReady, wtile.bufferIdx});
				}
			}
		}
#pragma omp barrier

		int tid = omp_get_thread_num();
		dset.endTxn(&r_txn);

#pragma omp barrier
		//printf(" - post loop, tid: %d\n", tid);
		if (tid == 0) {
			dset.sendCommand(Command{Command::EndLvl, (int32_t) lvl});

			for (int i=0; i<4; i++) lvlTlbr[i] >>= 1;
			nrows = lvlTlbr[2] - lvlTlbr[0];
			ncols = lvlTlbr[3] - lvlTlbr[1];
		}
#pragma omp barrier
		if (nrows <= 1 or ncols <= 1) {
			if (tid == 0) printf(" - Stopping Addo on lvl %lu (nrows %lu, ncols %lu)\n", lvl, nrows, ncols);
			break;
		}

		// Note: It is necessary to synchronize with the w_thread before moving onto next level,
		//       since we read parent tiles from it.
		if (tid == 0)
			dset.blockUntilEmptiedQueue();
#pragma omp barrier
	}

	return 0;
}

}

int main(int argc, char** argv) {

	if (argc == 1) {
		printf(" - You must provide the dset path as first arg.\n");
		return 1;
	}

	bool clean = false;
	bool cleanFromLastLevel = false;

	if (argc == 3) {
		if (strcmp(argv[2],"--clean") == 0)
			clean = argv;
		else if (strcmp(argv[2],"--cleanFromLastLevel") == 0)
			cleanFromLastLevel = argv;
		else {
			printf(" - third arg must be --clean, --cleanFromLastLevel or nothing\n");
			return 1;
		}
	}

	DatasetWritable dset { argv[1] };

	dset.configure(ADDO_THREADS, 8);

	std::vector<int> existingLvls = findExistingLvls(dset);

	if (clean or cleanFromLastLevel) {
		if (existingLvls.size() <= 1) {
			printf(" - Tried to clean overviews, but there were none!\n");
			return 1;
		}
	}

	if (clean) return safeRemoveOverviews(dset, existingLvls);
	if (cleanFromLastLevel) return removeOverviews(dset, existingLvls);


	// This is no longer true.
	/*
	if (existingLvls.size() > 1) {
		printf(" - Can only add overviews if there is one existing level, but there are %lu\n", existingLvls.size());
		printf(" - Run 'frastAddo %s --clean' first.\n", argv[1]);
		return 1;
	}
	return makeOverviews(dset, existingLvls);
	*/

	return safeMakeOverviews(dset, existingLvls);

}
