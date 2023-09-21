#include <locale>
#include <fmt/core.h>
#include <fmt/ostream.h>

#include "frast2/flat/reader.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include "frast2/detail/argparse.hpp"

#include <limits.h>
#include <sstream>
#include "frast2/flat/gdal_stuff.hpp"

using namespace frast;

#if FMT_VERSION == 80101
#else
template <typename T>
struct fmt::formatter< T, std::enable_if_t< std::is_base_of<Eigen::DenseBase<T>, T>::value, char>> : ostream_formatter {};
template <typename T> struct fmt::formatter<Eigen::WithFormat<T>> : ostream_formatter {};
#endif

static void do_show_overlap_(ArgParser& parser);


int main(int argc, char** argv) {

	ArgParser parser(argc, argv);

	std::locale::global(std::locale("en_US.UTF-8"));

	/*
	int i = parser.get<int>("--hi").value();
	bool o = parser.get<bool>("--opt").value();
	fmt::print(" - hi={}\n", i);
	fmt::print(" - opt={}\n", o);
	fmt::print(" - action={}\n", action);
	*/
	auto action = parser.getChoice2("-a", "--action", "info", "showTiles", "showSample", "rasterIo", "dump", "takeTop", "showOverlap").value();


	if (action == "showOverlap") {
		do_show_overlap_(parser);
		return 0;
	}

	std::string path = parser.get2OrDie<std::string>("-i", "--input");
	bool isTerrain = parser.get2<bool>("-t", "--terrain", 0).value();
	EnvOptions opts;
	opts.readonly = true;
	opts.isTerrain = isTerrain;
	FlatReaderCached reader(path, opts);


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

	if (action == "takeTop") {
		auto maxLvl_ = parser.get<int64_t>("--maxLevel");
		if (!maxLvl_.has_value()) {
			fmt::print("action `takeTop` requires specifiying `--maxLevel`\n");
			return 1;
		}
		int maxLvl = maxLvl_.value();

		std::vector<int64_t> levels;
		for (int i=maxLvl; i>=0; i--) if (reader.env.haveLevel(i)) levels.push_back(i);


		if (levels.size() == 0) {
			fmt::print("warning: input had no levels above specified `maxLvl` {}\n", maxLvl);
			return 1;
		}

		EnvOptions newOpts;
		newOpts.readonly = false;
		newOpts.isTerrain = false;
		FlatEnvironment newEnv(path + ".2", newOpts);

		for (int i=0; i<levels.size(); i++) {
		int lvl = levels[i];
		fmt::print("copying level {}.\n", lvl);
		auto& oldSpec = reader.env.meta()->levelSpecs[lvl];
		newEnv.copyLevelFrom(
			lvl,
			(uint8_t*)reader.env.getBasePointer(),
			oldSpec.keysOffset, oldSpec.keysLength,
			oldSpec.k2vsOffset, oldSpec.keysLength,
			oldSpec.valsOffset, oldSpec.valsLength,
			i == levels.size() - 1);
		}
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





static void do_show_overlap_(ArgParser& parser) {
		using Corners = Eigen::Matrix<double,4,2>;

		std::vector<std::string> datasetPaths = parser.get2OrDie<std::vector<std::string>>("-i", "--input");
		int N = datasetPaths.size();
		std::vector<Corners> cornerss(N);
		// std::vector<std::pair<int,int>> dsetSizes;
		Vector4d wmTlbr{
			std::numeric_limits<double>::max(),
			std::numeric_limits<double>::max(),
			-std::numeric_limits<double>::max(),
			-std::numeric_limits<double>::max() };

		// For each dataset, get corners and track global tlbr.
		for (int i=0; i<N; i++) {
			MyGdalDataset dset(datasetPaths[i], false);
			Corners corners = dset.getWmCorners();
			cornerss[i] = corners;
			for (int j=0; j<4; j++) {
				wmTlbr(0) = std::min(wmTlbr(0), corners(j,0));
				wmTlbr(1) = std::min(wmTlbr(1), corners(j,1));
				wmTlbr(2) = std::max(wmTlbr(2), corners(j,0));
				wmTlbr(3) = std::max(wmTlbr(3), corners(j,1));
			}
		}

		fmt::print(" - have wmTlbr : {}\n", wmTlbr.transpose());

		double ww = wmTlbr(2) - wmTlbr(0);
		double hh = wmTlbr(3) - wmTlbr(1);
		int w,h;
		int s = 2048;
		s = 1024;
		if (ww > hh) {
			w = s;
			h = (int)(s * hh/ww + .5);
		} else {
			h = s;
			w = (int)(s * ww/hh + .5);
		}

		bool do_svg = true;

		if (do_svg) {
			std::stringstream ss;
			// ss << fmt::format("<svg version=\"1.1\" width=\"{}\" height=\"{}\" viewBox=\"{}\" xmlns=\"http://www.w3.org/2000/svg\">\n",
			ss << fmt::format("<svg version=\"1.1\" viewBox=\"{}\" xmlns=\"http://www.w3.org/2000/svg\">\n",
					// w,h,
					// fmt::format("{:.1f} {:.1f} {:.1f} {:.1f}", wmTlbr(0), wmTlbr(1), wmTlbr(2), wmTlbr(3))
					fmt::format("{:.1f} {:.1f} {:.1f} {:.1f}", wmTlbr(0), wmTlbr(1), ww, hh)
				);

			// ss << " <style> path { mix-blend-mode: ; } </style> \n";
			ss << R"(<style>
.small {
	mix-blend-mode: difference;
}
</style>
)";


			auto pushQuad = [&ss](const Corners& c, const Vector3i& color, float alpha) {
				ss << fmt::format("<path d=\"M {} {} L {} {} L {} {} L {} {} Z\" fill=\"rgb({},{},{})\" fill-opacity=\"{}\"/>\n",
						c(0,0), c(0,1),
						c(1,0), c(1,1),
						c(2,0), c(2,1),
						c(3,0), c(3,1),
						color(0), color(1), color(2),
						alpha
						);
			};

			ss << fmt::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" color=\"black\"/>\n",
					(wmTlbr(0)),
					(wmTlbr(1)),
					ww,hh);

			for (int i=0; i<N; i++) {
				float v = 2.f * 3.141f * ((float)i) / (N-1);

				Vector3f color;
				
				if (0) {
					color = Vector3f {
						std::cos(v*.9f + .1f),
						std::cos(v*2.5f + 1.51f),
						std::cos(v*1.5f + 2.6f),
					};
					color = color.normalized();
				} else {
					// https://en.wikipedia.org/wiki/HSL_and_HSV
					float h = (v * 180 / 3.141) / 60;
					float c = 1;
					float x = c * (1 - std::abs(std::fmod(h, 2.f) - 1));
					if      (h >= 0 and h < 1) color << c, x, 0;
					else if (h >= 1 and h < 2) color << x, c, 0;
					else if (h >= 2 and h < 3) color << 0, c, x;
					else if (h >= 3 and h < 4) color << 0, x, c;
					else if (h >= 4 and h < 5) color << x, 0, c;
					else                  color << c, 0, x;
					// fmt::print("{} {} -> {} x={} :: {}\n", i, v, h, x, color.transpose());
				}

				pushQuad(cornerss[i], (color * 255.f).cast<int>(), .142f);
				Vector2d txtPos = cornerss[i].row(0) * .9 + .1 * cornerss[i].row(2);
				double dw = std::abs(cornerss[i](2,0) - cornerss[i](0,0));
				ss << fmt::format(R"( <text x="{:.1f}" y="{:.1f}" class="small" fill="white" style="font-size: {}em">{}</text> )",
						txtPos(0), txtPos(1),
						.1 * (dw / datasetPaths[i].length()),
						datasetPaths[i]
				);
			}

			ss << "</svg>\n";

			std::string outname = "overlaps.svg";
			std::ofstream ofs(outname);
			ofs << ss.str();
			fmt::print(" - wrote '{}'\n", outname);

			return;
		}

		/*
		// Create image.
		cv::Mat allTxt(h,w,CV_8UC4);
		cv::Mat allAcc(h,w,CV_8UC1);
		allTxt = cv::Scalar{0};
		allAcc = cv::Scalar{0};
		fmt::print(" - img size {} {}\n", w,h);

		auto alphaImpose = [](cv::Mat dst, cv::Mat src) {
			assert(src.size() == dst.size());
			for (int y=0; y<dst.rows; y++)
			for (int x=0; x<dst.cols; x++) {
				uint8_t ia = src.data[y*dst.cols*4 + x*4 + 3];
				if (ia > 0) {
					for (int c=0; c<dst.channels(); c++) {
						dst.data[y*dst.cols*dst.channels() + x*dst.channels() + c] = src.data[y*src.cols*4 + x*4 + c];
					}
				}
			}
		};

		// Start filling it.
		for (int i=0; i<N; i++) {
			int ss = 128;
			cv::Mat txt(ss,ss,CV_8UC4);
			cv::Mat acc(ss,ss,CV_8UC1);

			acc = cv::Scalar{1,0,0,0};
			txt = cv::Scalar{0,0,0,0};
			// cv::putText(txt, datasetPaths[i], cv::Point{21,21}, 0, .8, cv::Scalar{0,0,0,1});
			// cv::putText(txt, datasetPaths[i], cv::Point{20,20}, 0, .8, cv::Scalar{255,255,255,1});
			cv::putText(txt, datasetPaths[i], cv::Point{11,11}, 0, .7, cv::Scalar{0,0,0,1});
			cv::putText(txt, datasetPaths[i], cv::Point{10,10}, 0, .7, cv::Scalar{255,255,255,1});

			float from[] = {
				0,0,
				ss,0,
				ss,ss,
				0,ss };
			Corners c = cornerss[i];

			bool flipY = true;
			float to[8];
			if (flipY) {
				float to_[] = {
					(c(3,0) - wmTlbr(0)) / ww * w, (c(3,1) - wmTlbr(1)) / hh * h,
					(c(2,0) - wmTlbr(0)) / ww * w, (c(2,1) - wmTlbr(1)) / hh * h,
					(c(1,0) - wmTlbr(0)) / ww * w, (c(1,1) - wmTlbr(1)) / hh * h,
					(c(0,0) - wmTlbr(0)) / ww * w, (c(0,1) - wmTlbr(1)) / hh * h };
				memcpy(to,to_,sizeof(to_));
			} else {
				float to_[] = {
					(c(0,0) - wmTlbr(0)) / ww * w, (c(0,1) - wmTlbr(1)) / hh * h,
					(c(1,0) - wmTlbr(0)) / ww * w, (c(1,1) - wmTlbr(1)) / hh * h,
					(c(2,0) - wmTlbr(0)) / ww * w, (c(2,1) - wmTlbr(1)) / hh * h,
					(c(3,0) - wmTlbr(0)) / ww * w, (c(3,1) - wmTlbr(1)) / hh * h };
				memcpy(to,to_,sizeof(to_));
			}

			cv::Mat H = cv::getPerspectiveTransform((cv::Point2f*)from,(cv::Point2f*)to);

			float rs[8];
			cv::Mat from_(4,1,CV_32FC2,from);
			cv::Mat rs_(4,1,CV_32FC2,rs);
			cv::perspectiveTransform(from_,rs_,H);
			// fmt::print("\n"); for (int i=0; i<4; i++) fmt::print(" - warped {} {}\n", rs[i*2+0], rs[i*2+1]);

			cv::Mat wtxt, wacc;
			cv::warpPerspective(txt, wtxt, H, allTxt.size());
			cv::warpPerspective(acc, wacc, H, allTxt.size());

			cv::addWeighted(allAcc, 1, wacc, 1, 0, allAcc);
			// cv::imshow("acc",allAcc);
			// cv::waitKey(0);
			// cv::addWeighted(allTxt, 1, wtxt, 1, 0, allTxt);
			// for (int i=0; i<allTxt.total(); i++) allTxt.data[i] = wtxt.data[i];
			alphaImpose(allTxt, wtxt);
		}

		cv::Mat modAcc(h,w,CV_8UC3);
		float maxAcc = 1;
		for (int y=0; y<allAcc.rows; y++)
		for (int x=0; x<allAcc.cols; x++) {
			maxAcc = std::max((float)allAcc.data[y*1*allAcc.cols+x*1+0], maxAcc);
		}
		fmt::print(" - max overlap was {}\n", maxAcc);
		for (int y=0; y<allAcc.rows; y++)
		for (int x=0; x<allAcc.cols; x++) {
			uint8_t a = allAcc.data[y*1*allAcc.cols+x*1+0];
			float v = 2 * 3.141 * (((float)a) / maxAcc);
			// float v = ((x % 255) / 255.f) * 2 * 3.141;
			modAcc.data[y*3*allAcc.cols+x*3+0] = (uint8_t) (255.f * (.5f + .5f * std::cos(v*.9f+.1f)));
			modAcc.data[y*3*allAcc.cols+x*3+1] = (uint8_t) (255.f * (.5f));
			modAcc.data[y*3*allAcc.cols+x*3+2] = (uint8_t) (255.f * (.5f + .5f * std::cos(v*.7f+2.0f)));
			if (a == 0) {
				modAcc.data[y*3*allAcc.cols+x*3+0] = modAcc.data[y*3*allAcc.cols+x*3+1] = modAcc.data[y*3*allAcc.cols+x*3+2] = 0;
			}
		}

		cv::Mat modTxt(h,w,CV_8UC3);
		for (int y=0; y<allAcc.rows; y++)
		for (int x=0; x<allAcc.cols; x++) {
			modTxt.data[y*modTxt.cols*3+x*3+0] = allTxt.data[y*modTxt.cols*4+x*4+0];
			modTxt.data[y*modTxt.cols*3+x*3+1] = allTxt.data[y*modTxt.cols*4+x*4+1];
			modTxt.data[y*modTxt.cols*3+x*3+2] = allTxt.data[y*modTxt.cols*4+x*4+2];
		}

		cv::Mat dimg;
		// cv::addWeighted(modTxt, 1, modAcc, 1, 0, dimg);
		dimg = modAcc.clone();
		alphaImpose(dimg, allTxt);

		cv::imshow("Inputs", dimg);
		cv::waitKey(0);
		*/
}

