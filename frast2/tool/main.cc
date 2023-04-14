#include <fmt/core.h>

#include "frast2/flat/reader.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include "frast2/detail/argparse.hpp"

using namespace frast;

int main(int argc, char** argv) {

	ArgParser parser(argc, argv);

	/*
	int i = parser.get<int>("--hi").value();
	bool o = parser.get<bool>("--opt").value();
	fmt::print(" - hi={}\n", i);
	fmt::print(" - opt={}\n", o);
	fmt::print(" - action={}\n", action);
	*/
	auto action = parser.getChoice2("-a", "--action", "info", "showTiles", "showSample", "rasterIo").value();

	std::string path = parser.get2<std::string>("-i", "--input").value();
	bool isTerrain = parser.get2<bool>("-t", "--terrain", 0).value();
	EnvOptions opts;
	opts.readonly = true;
	opts.isTerrain = isTerrain;
	FlatReaderCached reader(path, opts);

	if (action == "info") {
		uint32_t tlbr[4];
		auto lvl = reader.determineTlbr(tlbr);
		fmt::print(" - Tlbr (lvl {}) [{} {} -> {} {}]\n", lvl, tlbr[0], tlbr[1], tlbr[2], tlbr[3]);
	}

	if (action == "showTiles") {
		int chosenLvl = parser.get2<int>("-l", "--level", -1).value();

		uint32_t tlbr[4];
		auto deepestLvl = reader.determineTlbr(tlbr);

		int lvl = deepestLvl;
		if (chosenLvl != -1 and chosenLvl < deepestLvl) {
			lvl = chosenLvl;
			int64_t zoom = deepestLvl - chosenLvl;
			for (int i=0; i<4; i++)
				tlbr[i] = tlbr[i] / (1 << zoom);
		}
		fmt::print(" - Tlbr (lvl {}) [{} {} -> {} {}]\n", lvl, tlbr[0], tlbr[1], tlbr[2], tlbr[3]);

		auto &spec = reader.env.getLevelSpec(lvl);
		fmt::print(" - Items {}\n", spec.nitemsUsed());
		uint64_t n = spec.nitemsUsed();
		auto keys = reader.env.getKeys(lvl);
		for (int i=0; i<spec.nitemsUsed(); i++) {
			auto key = keys[i];
			// Value val = reader.env.getValueFromIdx(lvl, i);
			Value val = reader.env.lookup(lvl, key);
			fmt::print(" - item ({:>6d}/{:>6d}) key {} len {}\n", i,n, key, val.len);


			if (val.value != nullptr) {
				cv::Mat img = decodeValue(val, opts.isTerrain?1:3, opts.isTerrain);

				cv::imshow("tile", img);
				cv::waitKey(0);
			}


		}
	}

	if (action == "showSample") {
		int edge = parser.get<int>("--edge", 1440).value();

		double dwmTlbr[4];
		uint32_t iwmTlbr[4];
		auto deepestLvl = reader.determineTlbr(iwmTlbr);
		iwm_to_dwm(dwmTlbr, iwmTlbr, deepestLvl);
		int nw = iwmTlbr[2]-iwmTlbr[0];
		int nh = iwmTlbr[3]-iwmTlbr[1];

		cv::Mat img;
		if (nw > nh) {
			img = reader.rasterIo(dwmTlbr, edge, nh*edge/nw, opts.isTerrain?1:3);
		} else {
			img = reader.rasterIo(dwmTlbr, nw*edge/nh, edge, opts.isTerrain?1:3);
		}

		cv::imshow("sample", img);
		cv::waitKey(0);
	}

	if (action == "rasterIo") {
		int edge = parser.get<int>("--edge", 1440).value();
		Tlbr tlbr = parser.get<Tlbr>("--tlbr").value();


		double dwmTlbr[4] = {tlbr.tl[0], tlbr.tl[1], tlbr.br[0], tlbr.br[1]};
		int nw = dwmTlbr[2]-dwmTlbr[0];
		int nh = dwmTlbr[3]-dwmTlbr[1];

		cv::Mat img;
		if (nw > nh) {
			img = reader.rasterIo(dwmTlbr, edge, (nh*edge)/nw, opts.isTerrain?1:3);
		} else {
			img = reader.rasterIo(dwmTlbr, (nw*edge)/nh, edge, opts.isTerrain?1:3);
		}

		cv::imshow("sample", img);
		cv::waitKey(0);
	}

	return 0;
}
