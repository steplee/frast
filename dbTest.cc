#include "db.h"
#include "image.h"

#include <chrono>
#include <iostream>
#include <iomanip>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>


static int image_signature(const Image& i) {
	int n = i.size();
	int acc = 0;
	for (int j=0; j<n; j++) acc += static_cast<int>(i.buffer[j]);
	return acc;
}



int dumpTile(Dataset& dset, uint64_t z, uint64_t y, uint64_t x, int w, int h) {
	Image img { 256, 256, 3 }; img.alloc();

	cv::Mat mat ( 256 * w, 256 * h, CV_8UC3 );

	for (uint64_t yy=y, yi=0; yy<y+h; yy++, yi++)
	for (uint64_t xx=x, xi=0; xx<x+h; xx++, xi++) {
		BlockCoordinate coord { z,yy,xx };
		if (dset.get(img, coord, nullptr))
			return 1;

		cv::Mat imgRef { img.h, img.w, CV_8UC3, img.buffer };
		imgRef.copyTo(mat(cv::Rect({((int)xi)*256, (int)(h-1-yi)*256, 256, 256})));
	}

	if (img.channels() == 3) cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
	cv::imwrite("out/tile_" + std::to_string(z) + "_" + std::to_string(y) + "_" + std::to_string(x) + ".jpg", mat);
	return 0;
}

int rasterIo_it(DatasetReader& dset, double tlbr[4]) {
	Image img {512,512,3};
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

int main(int argc, char** argv) {
	if (argc > 1 and strcmp(argv[1],"dumpTile") == 0) {
		assert(argc == 5 or argc == 7);
		int z = std::atoi(argv[2]);
		int y = std::atoi(argv[3]);
		int x = std::atoi(argv[4]);
		int w = argc == 7 ? std::atoi(argv[5]) : 1;
		int h = argc == 7 ? std::atoi(argv[6]) : 1;
		Dataset dset("out");
		return dumpTile(dset, z,y,x, w,h);
	}

	if (argc > 1 and strcmp(argv[1],"rasterIo") == 0) {
		assert(argc == 6);
		double tlbr[4] = {
			std::atof(argv[2]),
			std::atof(argv[3]),
			std::atof(argv[4]),
			std::atof(argv[5]) };
		DatasetReader dset("out");
		return rasterIo_it(dset, tlbr);
	}

	/*
	DatasetReader dset("out");

	Image img  { 256, 256, 1 }; img.calloc(0);
	//Image img2;
	Image img2 { 256, 256, 1 }; img2.calloc(0);

	for (int i=0; i<256; i++) img.buffer[i*256+i] = (uint8_t) i;
	//for (int i=0; i<256; i++) for (int j=0; j<256; j++) img.buffer[i*256+j] = (uint8_t)220;

	BlockCoordinate coord{20, 512,512};
	dset.createLevelIfNeeded(coord.z());

	//constexpr int N = 1e5;
	//constexpr int N = 1e2;
	constexpr int N = 1e3;
	constexpr int DatasetCols = 1e3;
	std::cout << std::fixed << std::setprecision(4) << std::dec;

	{
		AddTimeGuard g(_totalTime);

	// Put  N Images.
	if (1) {
		cv::Mat imgRef { img.h, img.w, CV_8U, img.buffer };
		auto st = std::chrono::high_resolution_clock::now();
		double acc = 0;

		MDB_txn* txn;
		dset.beginTxn(&txn);

		for (int i=0; i<N; i++) {
			auto st_ = std::chrono::high_resolution_clock::now();
			imgRef = cv::Scalar{0};
			imgRef(cv::Rect{0,0,1,256}) = cv::Scalar{(double)(i%255)};
			cv::putText(imgRef, "HI_"+std::to_string(i), {50,50}, 0, 1, cv::Scalar{255}, 1);
			auto et_ = std::chrono::high_resolution_clock::now();
			acc += getNanoDiff(et_,st_);

			BlockCoordinate coord_{20, (uint64_t)i/DatasetCols,(uint64_t)i%DatasetCols};
			dset.put(img, coord_, &txn);
			if (i % 1000 == 0) std::cout << " - wrote " << i << " (" << 100. * ((double)i) / N << "%)\n";
		}
		auto et = std::chrono::high_resolution_clock::now();
		std::cout << " - Put " << N << " images, " << prettyPrintNanos(getNanoDiff(et, st))
			<< " (actually " << prettyPrintNanos(getNanoDiff(et,st)-acc) << ")"
			<< " (" << prettyPrintNanos(getNanoDiff(et,st)/N) << "/put)\n"
			<< ").\n";

		dset.endTxn(&txn);
	}

	// Read N Images.
	{
		auto st = std::chrono::high_resolution_clock::now();
		double acc = 0;
		//MDB_txn* txn; dset.beginTxn(&txn);
		for (int i=0; i<N; i++) {
			//__builtin_ia32_pause();
			BlockCoordinate coord_{20, (uint64_t)i/DatasetCols,(uint64_t)i%DatasetCols};
			dset.get(img2, coord_, nullptr);
			//dset.get(img2, coord_, &txn);
		}
		auto et = std::chrono::high_resolution_clock::now();
		std::cout << " - Got " << N << " images, " << prettyPrintNanos(getNanoDiff(et, st)) << ".\n";

		//dset.endTxn(&txn);
	}
	}

	if (0) {
	if (dset.put(img, coord,nullptr))
		std::cout << " - failed to put.\n";
	std::cout << std::dec;


	if (dset.get(img2, coord, nullptr)) std::cout << " - failed to get.\n";
	std::cout << std::dec;

	std::cout << " - Input  signature: " << image_signature(img ) << "\n";
	std::cout << " - Output signature: " << image_signature(img2) << "\n";
	}


	printDebugTimes();
	*/


	return 0;
}
