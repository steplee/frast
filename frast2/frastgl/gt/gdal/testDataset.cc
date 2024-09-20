#include "dataset.h"

#include <opencv2/highgui.hpp>
#include <fmt/core.h>

using namespace frast;

// Temporary file to sanity check some stuff.

static void test_localTile_alignmnet_to_getWm() {
	// GdalDataset dset("/data/naip/nm1.tiff", false);
	GdalDataset dset("/data/naip/lsat1.2.tiff", false);
	GdalDataset dted("/data/elevation/srtm/usa.lzw.x1.halfRes.tiff", true);

	int X = 1;
	int Y = 1;
	int W = 20;
	int H = 20;
	int Z = 2;
	for (int y=Y; y<Y+H; y++) {
	for (int x=X; x<X+W; x++) {
		cv::Mat a = dset.getLocalTile(x,y, Z);
		cv::imshow("a", a);

		Vector4d tlbrWm = dset.getLocalTileBoundsWm(x,y,Z);
		fmt::print("tlbrWm [ {}, {}, {}, {} ]\n", tlbrWm(0), tlbrWm(1), tlbrWm(2), tlbrWm(3));
		
		cv::Mat d(256,256,CV_16UC1);
		cv::Mat d1(256,256,CV_16UC1);
		dted.setUseSubpixelOffsets(false);
		dted.getWm(tlbrWm, d);
		dted.setUseSubpixelOffsets(true);
		dted.getWm(tlbrWm, d1);
		double min,max;
		cv::minMaxLoc(d,&min,&max);
		d.convertTo(d, CV_8UC1, 255 / (max-min), -min/(max-min)*255);
		d1.convertTo(d1, CV_8UC1, 255 / (max-min), -min/(max-min)*255);
		cv::imshow("d (rounded to nearest integer offset/size)", d);
		cv::imshow("d (using subpixel precision)", d1);
		cv::Mat diff;
		cv::absdiff(d,d1,diff);
		diff = diff * 10;
		cv::imshow("d (diff)", diff);
		cv::waitKey(0);
	}
	}

}

int main() {

	test_localTile_alignmnet_to_getWm();

	return 0;
}
