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


template <class T>
double getMicroDiff(T b, T a) {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count() * 1e-3;
}
std::string prettyPrintMicros(double us) {
	std::string out              = "";
	if (us < 1'000) out          = std::to_string(us) + "μs";
	else if (us < 1'000'000) out = std::to_string(us/1e3) + "ms";
	else out                     = std::to_string(us/1e6) + "s";
	return out;
}

int dumpTile(Dataset& dset, uint64_t z, uint64_t y, uint64_t x) {
	Image img { 256, 256, 1 }; img.alloc();
	BlockCoordinate coord { z,y,x };
	if (dset.get(img, coord, nullptr))
		return 1;

	cv::Mat imgRef { img.h, img.w, CV_8U, img.buffer };
	cv::imwrite("out/tile_" + std::to_string(z) + "_" + std::to_string(y) + "_" + std::to_string(x) + ".jpg", imgRef);
	return 0;
}

int main(int argc, char** argv) {
	if (argc > 1 and strcmp(argv[1],"dumpTile") == 0) {
		assert(argc == 5);
		int z = std::atoi(argv[2]);
		int y = std::atoi(argv[3]);
		int x = std::atoi(argv[4]);
		Dataset dset(Dataset::OpenMode::READ_ONLY, "out");
		return dumpTile(dset, z,y,x);
	}

	Dataset dset(Dataset::OpenMode::READ_WRITE, "out");

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
			acc += getMicroDiff(et_,st_);

			BlockCoordinate coord_{20, (uint64_t)i/DatasetCols,(uint64_t)i%DatasetCols};
			dset.put(img, coord_, &txn);
			if (i % 1000 == 0) std::cout << " - wrote " << i << " (" << 100. * ((double)i) / N << "%)\n";
		}
		auto et = std::chrono::high_resolution_clock::now();
		std::cout << " - Put " << N << " images, " << prettyPrintMicros(getMicroDiff(et, st))
			<< " (actually " << prettyPrintMicros(getMicroDiff(et,st)-acc) << ")"
			<< " (" << prettyPrintMicros(getMicroDiff(et,st)/N) << "/put)\n"
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
		std::cout << " - Got " << N << " images, " << prettyPrintMicros(getMicroDiff(et, st)) << ".\n";

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

	std::cout << " - Timing 'encodeTime'   : " << prettyPrintMicros(_encodeTime * 1e-3) << " (" << (_encodeTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'decodeTime'   : " << prettyPrintMicros(_decodeTime * 1e-3) << " (" << (_decodeTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'imgMergeTime' : " << prettyPrintMicros(_imgMergeTime * 1e-3) << " (" << (_imgMergeTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'dbWriteTime'  : " << prettyPrintMicros(_dbWriteTime * 1e-3) << " (" << (_dbWriteTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'dbReadTime'   : " << prettyPrintMicros(_dbReadTime * 1e-3) << " (" << (_dbReadTime/_totalTime) * 100 << "%)\n";
	std::cout << " - Timing 'dbEndTxnTime' : " << prettyPrintMicros(_dbEndTxnTime * 1e-3) << " (" << (_dbEndTxnTime/_totalTime) * 100 << "%)\n";



	return 0;
}
