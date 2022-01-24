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
	int32_t c = dset.channels();
	int ts = dset.tileSize();
	Image img { ts, ts, c }; img.alloc();


	auto cv_type = c == 1 ? CV_8U : c == 3 ? CV_8UC3 : CV_8UC4;
	cv::Mat mat ( ts * w, ts * h, cv_type );

	for (uint64_t yy=y, yi=0; yy<y+h; yy++, yi++)
	for (uint64_t xx=x, xi=0; xx<x+h; xx++, xi++) {
		BlockCoordinate coord { z,yy,xx };
		if (dset.get(img, coord, nullptr)) {
			printf(" - accessed bad block, exiting.\n");
			return 1;
		}

		cv::Mat imgRef { img.h, img.w, cv_type, img.buffer };
		imgRef.copyTo(mat(cv::Rect({((int)xi)*ts, (int)(h-1-yi)*ts, ts, ts})));
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


	return 0;
}
