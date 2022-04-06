#include "db.h"
#include "image.h"

#include <chrono>
#include <iostream>
#include <iomanip>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>


namespace {
int image_signature(const Image& i) {
	int n = i.size();
	int acc = 0;
	for (int j=0; j<n; j++) acc += static_cast<int>(i.buffer[j]);
	return acc;
}



int dumpTile(Dataset& dset, uint64_t z, uint64_t y, uint64_t x, int w, int h) {
	auto f = dset.format();
	int ts = dset.tileSize();
	Image img { ts, ts, f }; img.alloc();

	if (x == -1 or y == -1) {
		uint64_t tlbr[4];
		dset.determineLevelAABB(tlbr, z);
		x = tlbr[0];
		y = tlbr[1];
		w = tlbr[2] - tlbr[0];
		h = tlbr[3] - tlbr[1];
	}

	auto c = dset.channels();
	auto cv_type = dset.format() == Image::Format::TERRAIN_2x8 ? CV_16UC1 : c == 1 ? CV_8U : c == 3 ? CV_8UC3 : CV_8UC4;
	cv::Mat mat ( ts * h, ts * w, cv_type );

	for (uint64_t yy=y, yi=0; yy<y+h; yy++, yi++)
	for (uint64_t xx=x, xi=0; xx<x+w; xx++, xi++) {
		BlockCoordinate coord { z,yy,xx };
		cv::Mat imgRef ( ts, ts, cv_type, img.buffer );
		if (dset.get(img, coord, nullptr)) {
			printf(" - accessed bad block %d %lu %lu.\n", z,yy,xx);
			imgRef = cv::Scalar{0};
			//return 1;
		}

		imgRef.copyTo(mat(cv::Rect({((int)xi)*ts, (int)(h-1-yi)*ts, ts, ts})));
	}

	if (img.channels() == 3) cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

	// Handle terrain case
	if (dset.format() == Image::Format::TERRAIN_2x8) {
		cv::Mat tmp = mat.clone();
		mat = cv::Mat(ts*h, ts*w, CV_8UC3);
		uint16_t min_ = 65535, max_ = 0;
		for (int y=0; y<tmp.rows; y++)
		for (int x=0; x<tmp.cols; x++) {
			uint16_t val = ((uint16_t*)tmp.data)[y*tmp.cols+x];
			if (val < min_) min_ = val;
			if (val > max_) max_ = val;
		}

		for (int y=0; y<tmp.rows; y++)
		for (int x=0; x<tmp.cols; x++) {
			uint16_t val_ = ((uint16_t*)tmp.data)[y*tmp.cols+x] - min_;
			float val = 255.999f * static_cast<float>(val_) / max_;
			((uint8_t*)mat.data)[y*tmp.cols*3+x*3+0] = val;
			((uint8_t*)mat.data)[y*tmp.cols*3+x*3+1] = val;
			((uint8_t*)mat.data)[y*tmp.cols*3+x*3+2] = val;
		}
		char a[32], b[32];
		sprintf(a, "MinVal %.1f", ((float)min_)/8);
		sprintf(b, "MaxVal %.1f", ((float)max_)/8);
		cv::putText(mat, a, {20,20}, 0, 1.f, cv::Scalar{0,255,0});
		cv::putText(mat, b, {20,50}, 0, 1.f, cv::Scalar{0,255,0});
	}

	cv::imwrite("out/tile_" + std::to_string(z) + "_" + std::to_string(y) + "_" + std::to_string(x) + ".jpg", mat);
	return 0;
}

int rasterIo_it(DatasetReader& dset, double tlbr[4]) {
	Image img {512,512,Image::Format::RGB};
	img.alloc();

	if (dset.rasterIo(img, tlbr)) {
		printf(" - rasterIo failed.\n");
		fflush(stdout);
		return 1;
	}

	cv::Mat mat ( img.h, img.w, CV_8UC3, img.buffer );
	if (img.channels() == 3) cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
	cv::imwrite("out/rasterIoed.jpg", mat);
	return 0;
}
int testGray() {
	Image img1 { 256, 256, Image::Format::RGB };
	Image img2 { 256, 256, Image::Format::GRAY };
	img1.calloc();
	img2.calloc();
	for (int i=0; i<256; i++)
	for (int j=0; j<256; j++)
	for (int k=0; k<3; k++) {
		if (k == 0 and std::abs(i-j) < 2) img1.buffer[i*256*3+j*3+k] = 200;
		if (k == 1 and std::abs(i-j) > 256-3) img1.buffer[i*256*3+j*3+k] = 200;
	}

	img1.makeGray(img2);

	cv::Mat mat1 ( img1.h, img1.w, CV_8UC3, img1.buffer );
	cv::Mat mat2 ( img1.h, img1.w, CV_8UC1, img2.buffer );
	cv::imwrite("out/grayTest1.jpg", mat1);
	cv::imwrite("out/grayTest2.jpg", mat2);

	return 0;
}
}

int do_subaoi(DatasetWritable& dsetOut, DatasetReader& dsetIn, double tlbr[4]) {
	if (tlbr[0] > tlbr[2]) std::swap(tlbr[0], tlbr[2]);
	if (tlbr[1] > tlbr[3]) std::swap(tlbr[1], tlbr[3]);


	for (int64_t lvl=24; lvl>=0; lvl--) {

			if (not dsetIn.hasLevel(lvl))
				continue;

			dsetOut.sendCommand(Command{Command::BeginLvl, (int32_t)lvl});
			dsetOut.blockUntilEmptiedQueue();

			MDB_txn* r_txn;
			if (dsetIn.beginTxn(&r_txn, true)) throw std::runtime_error("failed to begin txn");
			// MDB_txn* w_txn;
			// if (dsetOut.beginTxn(&w_txn)) throw std::runtime_error("failed to begin txn");

			Image primaImg { dsetIn.tileSize(), dsetIn.tileSize(), dsetIn.format() };
			primaImg.calloc();
			EncodedImage finalEimg;


			int tid = 0;

			uint64_t lvlTlbr[4] = {
				(uint64_t) (((.5 * tlbr[0] / WebMercatorMapScale) + .5) * (1lu << lvl)),
				(uint64_t) (((.5 * tlbr[1] / WebMercatorMapScale) + .5) * (1lu << lvl)),
				(uint64_t) (((.5 * tlbr[2] / WebMercatorMapScale) + .5) * (1lu << lvl) + 1),
				(uint64_t) (((.5 * tlbr[3] / WebMercatorMapScale) + .5) * (1lu << lvl) + 1)
			};
			printf(" - Lvl %ld, tlbr %lu %lu %lu %lu\n", lvl, lvlTlbr[0],lvlTlbr[1],lvlTlbr[2],lvlTlbr[3]);

			uint64_t lastRow = 0;
			int n_added = 0, n_seen = 0;
			dsetIn.iterLevel(lvl, r_txn, [&lvlTlbr, &lastRow,&dsetOut, &dsetIn, &primaImg, tid, &n_added, &n_seen](const BlockCoordinate& bc, MDB_val& val) {
					uint64_t x = bc.x(), y = bc.y();
					n_seen++;

					// Test if tile box is inside given box
					// if (x >= lvlTlbr[0] and x <= lvlTlbr[2] and y >= lvlTlbr[1] and y <= lvlTlbr[3]) {
					// Do symmetric bbox intersect test.
					if (x   <= lvlTlbr[2] and y   <= lvlTlbr[3] and
						x+1 >= lvlTlbr[2] and y+1 >= lvlTlbr[3]) {
						n_added++;
						auto &wtile = dsetOut.blockingGetTileBufferForThread(tid);
						wtile.fillWith(bc, val);
						dsetOut.sendCommand(Command{Command::TileReady, wtile.bufferIdx});
						// dsetOut.blockUntilEmptiedQueue();
					}

					if (lastRow != y and y % 4 == 0)
						printf(" - row %lu, added %d / %d\n", y, n_added, n_seen);
					lastRow = y;
			});

			// if (dsetOut.endTxn(&w_txn)) throw std::runtime_error("failed to end txn");
			if (dsetIn.endTxn(&r_txn)) throw std::runtime_error("failed to end txn");
	}



	return 0;
}

int main(int argc, char** argv) {
	if (argc > 1 and strcmp(argv[1],"testGray") == 0) {
		return testGray();
	}
	if (argc > 1 and strcmp(argv[1],"dumpTile") == 0) {
		assert(argc == 6 or argc == 8);
		int z = std::atoi(argv[3]);
		int y = std::atoi(argv[4]);
		int x = std::atoi(argv[5]);
		int w = argc == 8 ? std::atoi(argv[6]) : -1;
		int h = argc == 8 ? std::atoi(argv[7]) : -1;
		Dataset dset(std::string{argv[2]});
		return dumpTile(dset, z,y,x, w,h);
	}

	if (argc > 1 and strcmp(argv[1],"rasterIo") == 0) {
		assert(argc == 7);
		double tlbr[4] = {
			std::atof(argv[3]),
			std::atof(argv[4]),
			std::atof(argv[5]),
			std::atof(argv[6]) };
		DatasetReader dset(std::string{argv[2]});
		return rasterIo_it(dset, tlbr);
	}

	if (argc > 1 and strcmp(argv[1],"subaoi") == 0) {
		if (argc != 8) {
			printf(" - Usage:\n\t./frastDump subaoi in.ft out.ft <tlbr>\n");
			return 1;
		}

		double tlbr[4] = {
			std::atof(argv[4]),
			std::atof(argv[5]),
			std::atof(argv[6]),
			std::atof(argv[7]) };
		DatasetReader dsetIn(std::string{argv[2]});
		DatabaseOptions dopts;
		DatasetWritable dsetOut(std::string{argv[3]}, dopts);
		dsetOut.configure(1,32);
		return do_subaoi(dsetOut, dsetIn, tlbr);
	}


	return 0;
}
