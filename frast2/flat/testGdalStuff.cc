
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>
#include <unistd.h>
#include <atomic>
#include <unordered_set>
#include <fcntl.h>

#include "gdal_stuff.hpp"

using namespace frast;

TEST_CASE( "GetWmTile", "[gdal]" ) {

	{
		MyGdalDataset dset("/data/naip/mocoNaip/whole.wm.tif");
		double tlbr[] = { -8602509.802070, 4757211.329385, -8597636.941517, 4751841.628147 };
		cv::Mat img = dset.getWmTile(tlbr, 512,512,1);
	}

	{
		MyGdalDataset dset("/data/naip/khop2/all.gray.tif");
		// double tlbr[] = {-9763630.063221,4391186.050634, -9747202.073783,4378140.996861};
		double tlbr[] = {-9763630.063221,4391186.050634, -9758646.876567,4387068.304317};
		cv::Mat img = dset.getWmTile(tlbr, 2048,2048,1);
	}
}
