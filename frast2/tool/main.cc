#include <locale>
#include <fmt/core.h>

#include "frast2/flat/reader.h"

#include <opencv2/imgproc.hpp>
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
	auto action = parser.getChoice2("-a", "--action", "info", "showTiles", "showSample", "rasterIo", "dump").value();

	std::string path = parser.get2OrDie<std::string>("-i", "--input");
	bool isTerrain = parser.get2<bool>("-t", "--terrain", 0).value();
	EnvOptions opts;
	opts.readonly = true;
	opts.isTerrain = isTerrain;
	FlatReaderCached reader(path, opts);

	std::locale::global(std::locale("en_US.UTF-8"));

	if (action == "info") {
		uint32_t tlbr[4];
		auto lvl = reader.determineTlbr(tlbr);
		fmt::print(" - Tlbr (lvl {}) [{} {} -> {} {}]\n", lvl, tlbr[0], tlbr[1], tlbr[2], tlbr[3]);
		fmt::print(" - Tlbr [{:.2f} {:.2f} -> {:.2f} {:.2f}]\n",
				(2*WebMercatorMapScale) * ((tlbr[0] / (double)(1<<lvl)) - .5),
				(2*WebMercatorMapScale) * ((tlbr[1] / (double)(1<<lvl)) - .5),
				(2*WebMercatorMapScale) * ((tlbr[2] / (double)(1<<lvl)) - .5),
				(2*WebMercatorMapScale) * ((tlbr[3] / (double)(1<<lvl)) - .5)
				);
		double tileToM  = (     2*WebMercatorMapScale) / (1<<lvl);
		double tileToKm = (.001*2*WebMercatorMapScale) / (1<<lvl);
		double wm_y = 2 * ((tlbr[1] + tlbr[3]) / 2.0) / (1<<lvl) - .5;
		double scaleFactor = cosh(wm_y);
		double scaleFactorInv = 1.0/scaleFactor;
		fmt::print(" - Scale Factor: {:.5f}\n", scaleFactor);
		int64_t w = tlbr[2] - tlbr[0];
		int64_t h = tlbr[3] - tlbr[1];
		int64_t n_tile = w*h;
		double log2_pixels = log2((double)n_tile * 256*256);
		fmt::print(" - deepest tile area count ({}w x {}h) (2^{:.2f} pix)\n", w,h, log2_pixels);
		fmt::print(" - deepest tile actual count: {}", reader.levelSize(lvl));
		fmt::print(" - meter [ wm    ] ({:.1Lf}m x {:.1Lf}m) ({:.2Lf}km²)\n", w*tileToM, h*tileToM, (w*tileToKm)*(h*tileToKm));
		fmt::print(" - meter [~actual] ({:.1Lf}m x {:.1Lf}m) ({:.2Lf}km²)\n", scaleFactorInv*w*tileToM, scaleFactorInv*h*tileToM, (w*scaleFactorInv*tileToKm)*(h*scaleFactorInv*tileToKm));
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

				cv::cvtColor(img,img,cv::COLOR_BGR2RGB);
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

		cv::cvtColor(img,img,cv::COLOR_BGR2RGB);
		cv::imshow("sample", img);
		cv::waitKey(0);
	}

	if (action == "rasterIo") {
		int edge = parser.get<int>("--edge", 1440).value();
		Tlbr tlbr = parser.get<Tlbr>("--tlbr").value();
		std::string out = parser.get2<std::string>("--out", "-o", std::string{""}).value();


		double dwmTlbr[4] = {tlbr.tl[0], tlbr.tl[1], tlbr.br[0], tlbr.br[1]};
		int nw = dwmTlbr[2]-dwmTlbr[0];
		int nh = dwmTlbr[3]-dwmTlbr[1];

		cv::Mat img;
		if (nw > nh) {
			img = reader.rasterIo(dwmTlbr, edge, (nh*edge)/nw, opts.isTerrain?1:3);
		} else {
			img = reader.rasterIo(dwmTlbr, (nw*edge)/nh, edge, opts.isTerrain?1:3);
		}

		cv::cvtColor(img,img,cv::COLOR_BGR2RGB);
		cv::imshow("sample", img);
		if (out.length())
			cv::imwrite(out, img);
		cv::waitKey(0);
	}

	if (action == "dump") {
		std::string outPath = parser.get2OrDie<std::string>("--out", "-o");
		int chosenLvl = parser.get2<int>("-l", "--level", -1).value();

		uint32_t tlbr[4];
		auto lvl = reader.determineTlbrOnLevel(tlbr, chosenLvl);

		/*
		int lvl = deepestLvl;
		if (chosenLvl != -1 and chosenLvl < deepestLvl) {
			lvl = chosenLvl;
			int64_t zoom = deepestLvl - chosenLvl;
			for (int i=0; i<4; i++)
				tlbr[i] = tlbr[i] / (1 << zoom);
		}
		*/

		fmt::print(" - Tlbr (lvl {}) [{} {} -> {} {}]\n", lvl, tlbr[0], tlbr[1], tlbr[2], tlbr[3]);
		uint32_t ny = tlbr[3] - tlbr[1];
		uint32_t nx = tlbr[2] - tlbr[0];
		if (nx * ny >= 32*32) {
			fmt::print(" - Refusing to 'dump' too large image (ny={}, nx={})\n", ny,nx);
			exit(1);
		} else
			fmt::print(" - 'dump' image (ny={}, nx={})\n", ny,nx);

		cv::Mat out(ny*256, nx*256, CV_8UC3);

		auto &spec = reader.env.getLevelSpec(lvl);
		fmt::print(" - Items {}\n", spec.nitemsUsed());
		uint64_t n = spec.nitemsUsed();
		auto keys = reader.env.getKeys(lvl);
		for (int i=0; i<spec.nitemsUsed(); i++) {
			auto key = keys[i];
			BlockCoordinate bc(key);
			// Value val = reader.env.getValueFromIdx(lvl, i);
			Value val = reader.env.lookup(lvl, key);
			int ix = ((int)bc.x()) - ((int)tlbr[0]);
			int iy = ((int)bc.y()) - ((int)tlbr[1]);
			fmt::print(" - item ({:>6d}/{:>6d}) key {} len {}, local {} {}\n", i,n, key, val.len, iy,ix);

			cv::Mat img = decodeValue(val, opts.isTerrain?1:3, opts.isTerrain);
			int th = img.rows;
			int tw = img.cols;
			if (ix >= 0 and iy >= 0 and ix < nx and iy < ny)
				img.copyTo(out(cv::Rect{256*(ix),256*(ny-1-iy),tw,th}));
			else
				fmt::print(" - skip out of bounds tile.\n");
		}

		cv::cvtColor(out, out, cv::COLOR_RGB2BGR);
		cv::imwrite(outPath, out);


	}

	return 0;
}
