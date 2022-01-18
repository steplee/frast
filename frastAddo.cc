#include "db.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <omp.h>


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

int removeOverviews(DatasetWritable& dset, const std::vector<int>& existingLvls) {
	int baseLvl = existingLvls[0];

	for (int i=0; i<existingLvls.size()-1; i++) {
		printf(" - (sending cmd to erase lvl %d)\n", existingLvls[i]);
		dset.sendCommand(Command{Command::EraseLvl, existingLvls[i]});
	}

	return 0;
}

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
					printf(" - Strange: tile %luz %luy %lux was missing all parents?\n", lvl,y,x);
				} else {
					printf(" - [thr %d] making tile %lu %lu %lu, with %d parents\n", tid, lvl,y,x, 4-nMissingParents);
				}

				// Downsample
				cv::resize(parent, child, cv::Size{tileSize,tileSize});

				// Put
				WritableTile& wtile = dset.blockingGetTileBufferForThread(tid);
				encode_cv__(wtile.eimg, child);
				wtile.coord = BlockCoordinate { lvl, y, x };
				//dset.push(wtile);
				dset.sendCommand(Command{Command::TileReady, wtile.bufferIdx});
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
}

int main(int argc, char** argv) {

	if (argc == 1) {
		printf(" - You must provide the dset path as first arg.\n");
		return 1;
	}

	bool clean = false;

	if (argc == 3) {
		if (strcmp(argv[2], "-c") == 0 or strcmp(argv[2],"--clean") == 0)
			clean = argv;
		else {
			printf(" - third arg must be -c or --clean or nothing");
			return 1;
		}
	}

	DatasetWritable dset { argv[1] };

	dset.configure(256,256, 3, ADDO_THREADS, 8);

	std::vector<int> existingLvls = findExistingLvls(dset);

	if (clean) {
		if (existingLvls.size() <= 1) {
			printf(" - Tried to clean overviews, but there were none!\n");
			return 1;
		}
		return removeOverviews(dset, existingLvls);
	}


	if (existingLvls.size() > 1) {
		printf(" - Can only add overviews if there is one existing level, but there are %lu\n", existingLvls.size());
		printf(" - Run 'frastAddo %s --clean' first.\n", argv[1]);
		return 1;
	}

	return makeOverviews(dset, existingLvls);
}
