#include "../db.h"
#include "../utils/memcpy_utils.hpp"

// #include <opencv2/imgproc.hpp>
// #include <opencv2/imgcodecs.hpp>
// #include <opencv2/highgui.hpp>
#include <algorithm>

#include <omp.h>
#include <chrono>

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

// Moved to makefile
//#define ADDO_THREADS 4
static_assert(ADDO_THREADS <= DatasetWritable::MAX_THREADS);

namespace {
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
		dset.sendCommand(DbCommand{DbCommand::EraseLvl, existingLvls[i]});
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
		//dset.sendCommand(DbCommand{DbCommand::EraseLvl, existingLvls[i]});
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

	auto format = dset.format();
	assert(format == Image::Format::RGB or format == Image::Format::GRAY or format == Image::Format::TERRAIN_2x8);
	int channels = dset.channels();
	int tileSize = dset.tileSize();

	// auto cv_type = format == Image::Format::TERRAIN_2x8 ? CV_16UC1 : channels == 1 ? CV_8U : channels == 3 ? CV_8UC3 : CV_8UC4;
	// cv::Mat tmpMat_[ADDO_THREADS];
	// cv::Mat parent_[ADDO_THREADS];
	// cv::Mat child_[ADDO_THREADS];
	Image parent_[ADDO_THREADS];
	Image child_[ADDO_THREADS];

	Image tmpImage_[ADDO_THREADS];
	//MDB_txn* r_txn_[ADDO_THREADS];
	EncodedImage eimg_[ADDO_THREADS];

	for (int i=0; i<ADDO_THREADS; i++) {
		tmpImage_[i] = Image { tileSize, tileSize, format };
		tmpImage_[i].alloc();
		// tmpMat_[i]   = cv::Mat ( tileSize, tileSize, cv_type, tmpImage_[i].buffer );
		// parent_[i]   = cv::Mat ( tileSize*2, tileSize*2, cv_type );
		// child_[i]    = cv::Mat ( tileSize  , tileSize  , cv_type );
		//r_txn_[i]    = nullptr;
		parent_[i] = Image { 2*tileSize, 2*tileSize, format };
		child_[i] = Image { tileSize, tileSize, format };
		parent_[i].alloc();
		child_[i].alloc();
	}

#pragma omp parallel num_threads(ADDO_THREADS) shared(lvlTlbr)
	for (uint64_t lvl=baseLvl-1; ; lvl--) {

		{
			int tid = omp_get_thread_num();
			if (tid == 0) {
				dset.sendCommand(DbCommand{DbCommand::BeginLvl, (int32_t) lvl});
				dset.blockUntilEmptiedQueue();
			}
		}
#pragma omp barrier
#pragma omp barrier

//#pragma omp for schedule(static)
#pragma omp for schedule(dynamic,4)
		for (uint64_t y=lvlTlbr[1]; y<=lvlTlbr[3]; y++) {

			MDB_txn* r_txn = nullptr;
			if (dset.beginTxn(&r_txn, true)) {
				printf(" - beginTxn failed.\n"); fflush(stdout);
				throw std::runtime_error("beginTxn failed");
			}

			int tid = omp_get_thread_num();
			Image& tmpImage = tmpImage_[tid];
			Image& parent = parent_[tid];
			Image& child = child_[tid];
			EncodedImage& eimg = eimg_[tid];
			int tilesInRow = 0;
			auto startTime = std::chrono::high_resolution_clock::now();


			for (uint64_t x=lvlTlbr[0]; x<=lvlTlbr[2]; x++) {
				BlockCoordinate myCoord { lvl, y, x };
				//printf(" - [thr %d] checking if existing tile %luz %luy %lux with txn %p\n", tid, lvl,y,x, r_txn);

				/*
				if (dset.tileExists(myCoord, r_txn)) {
					// Skip.
					// TODO: Actually: should merge with downsampled version.
					dprintf(" - [thr %d] skipping existing tile %luz %luy %lux\n", tid, lvl,y,x);
					continue;
				}
				*/

				// Get four parents
				int nMissingParents = 0;
				//printf(" - making tile %luz %luy %lux\n", lvl,y,x);
				for (uint64_t j=0; j<4; j++) {
					BlockCoordinate pcoord { lvl+1, y*2 + j/2, x*2 + j%2 };
					if (dset.get(tmpImage, pcoord, &r_txn)) {
						// Tile didn't exist, just make it black.
						memset(tmpImage.buffer, 0, tmpImage.size());
						nMissingParents++;
					}
					//cv::Mat tmpMat  = cv::Mat ( tileSize, tileSize, cv_type, tmpImage.buffer );
					//cv::imshow("child",tmpMat);
					//cv::waitKey(1);
					{
						// AtomicTimerMeasurement tg(t_memcpyStrided);
						// tmpMat.copyTo(parent(cv::Rect{tileSize*(((int32_t)j)%2), tileSize*(1-((int32_t)j)/2), tileSize, tileSize}));
						//inline void memcpyStridedOutputFlatInput(T* dst, const T* src, size_t rowStride, size_t w, size_t h) {
						auto rowStride = tileSize*2;
						uint8_t* src = tmpImage.buffer;
						uint8_t* dst = parent.buffer + parent.eleSize()*tileSize*channels*(j%2) + parent.eleSize()*channels*rowStride*(1-j/2)*tileSize;
						// uint8_t* dst = parent.buffer + tileSize*channels*(j%2) + channels*rowStride*(j/2)*tileSize;
						if (format == Image::Format::TERRAIN_2x8) memcpyStridedOutputFlatInput<uint16_t,1>((uint16_t*)dst, (uint16_t*)src, rowStride, tileSize, tileSize);
						else if (channels == 1) memcpyStridedOutputFlatInput<uint8_t,1>(dst, src, rowStride, tileSize, tileSize);
						else if (channels == 3) memcpyStridedOutputFlatInput<uint8_t,3>(dst, src, rowStride, tileSize, tileSize);
						else if (channels == 4) memcpyStridedOutputFlatInput<uint8_t,4>(dst, src, rowStride, tileSize, tileSize);
					}
				}

				if (nMissingParents == 4) {
					//printf(" - [thr %d] Strange: tile %luz %luy %lux was missing all parents? Skipping it.\n", tid, lvl,y,x);
				} else {
					dprintf(" - [thr %d] making tile %lu %lu %lu, with %d parents\n", tid, lvl,y,x, 4-nMissingParents);

					// Downsample
					{
						// AtomicTimerMeasurement tg(t_warp);
						// cv::resize(parent, child, cv::Size{tileSize,tileSize});
						// constexpr float H[6] = { .5, 0, 0, 0, .5, 0, };
						// parent.warpAffine(child, H);
						parent.halfscale(child);
					}

					//if (tid == 0) { cv::imshow("parent",parent); cv::waitKey(1); }

					// Put
					WritableTile& wtile = dset.blockingGetTileBufferForThread(tid);
					// Image childImg { child.cols, child.rows, format, child.data };
					{
						// AtomicTimerMeasurement tg(t_encodeImage);
						// encode(wtile.eimg, childImg);
						encode(wtile.eimg, child);
					}
					wtile.coord = myCoord;
					dset.sendCommand(DbCommand{DbCommand::TileReady, wtile.bufferIdx});
					tilesInRow++;
				}
			}

			if (tid == 0) {
				float yyy = y - lvlTlbr[1];
				auto endTime = std::chrono::high_resolution_clock::now();
				double seconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime-startTime).count() * 1e-9;
				double tps = tilesInRow / seconds;
				printf(" - ~%.2f%% finished (row %d / %d, %d tiles, %.2f tile/sec)\n", 100.f * yyy / nrows, y-lvlTlbr[1], nrows, tilesInRow, tps); fflush(stdout);
			}
			dset.endTxn(&r_txn);
		}
#pragma omp barrier
			int tid = omp_get_thread_num();


#pragma omp barrier
		//printf(" - post loop, tid: %d\n", tid);
		if (tid == 0) {
			dset.sendCommand(DbCommand{DbCommand::EndLvl, (int32_t) lvl});

		}
#pragma omp barrier
		if (nrows <= 1 and ncols <= 1) {
			 printf(" - Stopping [thr %d] Addo on lvl %lu [%lu %lu -> %lu %lu] (nrows %lu, ncols %lu)\n", tid, lvl,
					lvlTlbr[0], lvlTlbr[1], lvlTlbr[2], lvlTlbr[3],
					nrows, ncols);
			break;
		}
#pragma omp barrier
		if (tid == 0) {
			for (int i=0; i<4; i++) lvlTlbr[i] >>= 1lu;
			ncols = lvlTlbr[2] - lvlTlbr[0];
			nrows = lvlTlbr[3] - lvlTlbr[1];
			lvlTlbr[3] += 1;
			lvlTlbr[2] += 1;
		}
#pragma omp barrier

		// Note: It is necessary to synchronize with the w_thread before moving onto next level,
		//       since we read parent tiles from it.
		if (tid == 0) dset.blockUntilEmptiedQueue();
		//usleep(50'000);
#pragma omp barrier
	}

	{
		MDB_txn* txn;
		dset.beginTxn(&txn, false);
		dset.recompute_meta_and_write_slow(txn);
		dset.endTxn(&txn);
	}

	return 0;
}

int eraseLevels(DatasetWritable& dset, const std::vector<int>& existingLvls, std::vector<int>& toErase) {
	for (int lvl : toErase) {
		if (std::find(existingLvls.begin(), existingLvls.end(), lvl) == existingLvls.end()) {
			printf(" - Cannot drop level %d, it did not exist.\n", lvl);
			continue;
		}

		printf(" - Erasing lvl %d\n", lvl);
		MDB_txn* txn;
		if (dset.beginTxn(&txn)) throw std::runtime_error("Failed to open txn.");
		dset.dropLvl(lvl,txn);
		if (dset.endTxn(&txn)) throw std::runtime_error("Failed to open txn.");
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
	std::vector<int> toErase;

	if (argc >= 3) {
		if (strcmp(argv[2],"--clean") == 0)
			clean = argv;
		else if (strcmp(argv[2],"--cleanFromLastLevel") == 0)
			cleanFromLastLevel = argv;
		else if (strcmp(argv[2],"--erase") == 0) {
			if (argc == 3) throw std::runtime_error("You must provide zero or more levels to erase.");
			for (int i=3; i<argc; i++) {
				toErase.push_back(std::stoi(argv[i]));
			}
		} else {
			printf(" - third arg must be --clean, --cleanFromLastLevel or nothing\n");
			return 1;
		}
	}

	// AtomicTimerMeasurement _tg_total(t_total);
	DatasetWritable dset { argv[1] };

	dset.configure(ADDO_THREADS, WRITER_NBUF);

	std::vector<int> existingLvls = findExistingLvls(dset);

	if (clean or cleanFromLastLevel) {
		if (existingLvls.size() <= 1) {
			printf(" - Tried to clean overviews, but there were none!\n");
			return 1;
		}
	}

	if (clean) return safeRemoveOverviews(dset, existingLvls);
	if (toErase.size()) return eraseLevels(dset, existingLvls, toErase);
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
