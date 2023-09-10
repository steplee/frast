#include "reader.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

#include "frast2/errors.h"

namespace {
	using namespace frast;

	int get_cv_type_from_channels(int c, bool isTerrain) {
		if (isTerrain) {
			assert(c == 1 and "if isTerrain, only 1-channel 16-bit images supported");
			return CV_16UC1;
		}
		if (c == 1) return CV_8UC1;
		if (c == 3) return CV_8UC3;
		if (c == 4) return CV_8UC4;
		assert(false and "invalid num channels");
		return false;
	}


}

namespace frast {

	void dwm_to_iwm(uint32_t iwmTlbr[4], const double wmTlbr[4], int lvl) {
		// assert(lvl >= 0 and lvl < 30);
		if (lvl < 0 or lvl >= 30) throw InvalidLevelError(lvl);

		// iwmTlbr[0] = static_cast<uint32_t>((wmTlbr[0]+WebMercatorMapScale) / (2<<lvl));
		iwmTlbr[0] = static_cast<uint32_t>(    ((wmTlbr[0]+WebMercatorMapScale) / (2*WebMercatorMapScale)) * (1<<lvl));
		iwmTlbr[1] = static_cast<uint32_t>(    ((wmTlbr[1]+WebMercatorMapScale) / (2*WebMercatorMapScale)) * (1<<lvl));
		iwmTlbr[2] = static_cast<uint32_t>(1 + ((wmTlbr[2]+WebMercatorMapScale) / (2*WebMercatorMapScale)) * (1<<lvl));
		iwmTlbr[3] = static_cast<uint32_t>(1 + ((wmTlbr[3]+WebMercatorMapScale) / (2*WebMercatorMapScale)) * (1<<lvl));
	}

	void iwm_to_dwm(double dwmTlbr[4], const uint32_t iwmTlbr[4], int lvl) {
		// assert(lvl >= 0 and lvl < 30);
		if (lvl < 0 or lvl >= 30) throw InvalidLevelError(lvl);

		// iwmTlbr[0] = static_cast<uint32_t>((wmTlbr[0]+WebMercatorMapScale) / (2<<lvl));
		dwmTlbr[0] = (static_cast<double>(iwmTlbr[0]) / (1<<lvl) - .5) * (2*WebMercatorMapScale);
		dwmTlbr[1] = (static_cast<double>(iwmTlbr[1]) / (1<<lvl) - .5) * (2*WebMercatorMapScale);
		dwmTlbr[2] = (static_cast<double>(iwmTlbr[2]) / (1<<lvl) - .5) * (2*WebMercatorMapScale);
		dwmTlbr[3] = (static_cast<double>(iwmTlbr[3]) / (1<<lvl) - .5) * (2*WebMercatorMapScale);
	}




	FlatReader::FlatReader(const std::string& path, const EnvOptions& opts)
		: openPath(path), openOpts(opts), env(path, opts) {

	}

	int FlatReader::levelSize(int lvl) {
		return env.getLevelSpec(lvl).nitemsUsed();
	}

	void FlatReader::refreshMemMap() {
		// env = std::move(FlatEnvironment{openPath,openOpts});
		env = (FlatEnvironment(openPath,openOpts));
	}

	bool FlatReader::tileExists(uint64_t tile) {
		BlockCoordinate bc{tile};
		return env.keyExists(bc.z(), tile);
	}


	int FlatReader::determineDeepeseLevel() {
		int deepest = -1;
		for (int i=0; i<26; i++) {
			if (env.meta()->levelSpecs[i].keysCapacity > 0) deepest = i;
		}
		return deepest;
	}

	int64_t FlatReader::determineTlbr(uint32_t tlbr[4]) {
		auto lvl = determineDeepeseLevel();

		for (int i=0; i<4; i++) tlbr[i] = 0;
		if (lvl == -1) return lvl;

		return determineTlbrOnLevel(tlbr, lvl);
	}

	int64_t FlatReader::determineTlbrOnLevel(uint32_t tlbr[4], int lvl) {
		tlbr[0] = std::numeric_limits<uint32_t>::max();
		tlbr[1] = std::numeric_limits<uint32_t>::max();
		tlbr[2] = std::numeric_limits<uint32_t>::min();
		tlbr[3] = std::numeric_limits<uint32_t>::min();

		uint64_t* keys = env.getKeys(lvl);
		uint64_t n = env.meta()->levelSpecs[lvl].nitemsUsed();

		for (uint64_t i=0; i<n; i++) {
			BlockCoordinate bc(keys[i]);
			tlbr[0] = std::min(tlbr[0], (uint32_t)bc.x());
			tlbr[1] = std::min(tlbr[1], (uint32_t)bc.y());
			tlbr[2] = std::max(tlbr[2], 1u+(uint32_t)bc.x());
			tlbr[3] = std::max(tlbr[3], 1u+(uint32_t)bc.y());
		}

		return lvl;
	}

	std::vector<std::array<double,4>> FlatReader::computeRegionsOnDeepestLevel() {
		int lvl = determineDeepeseLevel();
		// This should never happen. File must be corrupted.
		if (lvl < 0 or lvl > 30) throw std::runtime_error("invalid level from determineDeepeseLevel()");

		uint64_t* keys = env.getKeys(lvl);
		auto nitems = env.getLevelSpec(lvl).nitemsUsed();

		uint32_t tlbr[4] = {
			std::numeric_limits<uint32_t>::max(),
			std::numeric_limits<uint32_t>::max(),
			std::numeric_limits<uint32_t>::min(),
			std::numeric_limits<uint32_t>::min() };

		for (int i=0; i<nitems; i++) {
			BlockCoordinate c(keys[i]);
			tlbr[0] = std::min(tlbr[0], (uint32_t)c.x());
			tlbr[1] = std::min(tlbr[1], (uint32_t)c.y());
			tlbr[2] = std::max(tlbr[2], 1u+(uint32_t)c.x());
			tlbr[3] = std::max(tlbr[3], 1u+(uint32_t)c.y());
		}

		std::array<double,4> dwmTlbr;
		iwm_to_dwm(dwmTlbr.begin(), tlbr, lvl);

		std::vector<std::array<double,4>> out;
		out.push_back(dwmTlbr);
		return out;
	}

	cv::Mat FlatReader::getTile(uint64_t tile, int channels) {
		BlockCoordinate bc(tile);
		auto val = env.lookup(bc.z(), tile);
		// fmt::print(" - found tile {} :: {} {}\n", tile, val.value, val.len);
		return decodeValue(val, channels, isTerrain());
	}

	bool FlatReader::getTile(cv::Mat& out, uint64_t tile, int channels) {
		BlockCoordinate bc(tile);
		auto val = env.lookup(bc.z(), tile);
		// fmt::print(" - found tile {} :: {} {}\n", tile, val.value, val.len);
		return decodeValue(out, val, channels, isTerrain());
	}

	// FIXME: This will fail if we have differing levels in different places in one large file.
	// For example if you merge a dataset at level 15 and another at level 17 into the same file,
	// sampling from the area with level 15 *may choose* level 17 and not read any good tiles.
	// Then you end up with black pixels, even though there is data at a higher level.
	// To fix this, either:
	//		1) Upscale data so that we always have the same base level after merging datasets.
	//		2) Have this function search upwards from the chosen level *at the chosen spot* to see if there are tiles
	// NOTE: This is a not an issue if you do not merge mixed resolution datasets (which I do not right now)
	int FlatReader::find_level_for_mpp(float res) {
		// First find the level that best matches the mpp.
		// Then search upward until we find a level that actually exists.
		// If no search level exists, search downward.
		//
		// If we still have none, throw an exception.
		//

			/*constexpr double WebMercatorCellSizes[MAX_LVLS] = {
				40075016.685578495, 20037508.342789248, 10018754.171394624, 5009377.085697312,	2504688.542848656,
				1252344.271424328,	626172.135712164,	313086.067856082,	156543.033928041,	78271.5169640205,
				39135.75848201025,	19567.879241005125, 9783.939620502562,	4891.969810251281,	2445.9849051256406,
				1222.9924525628203, 611.4962262814101,	305.7481131407051,	152.87405657035254, 76.43702828517627,
				38.218514142588134, 19.109257071294067, 9.554628535647034,	4.777314267823517,	2.3886571339117584,
				1.1943285669558792};*/

		int lvl = 0;

		for (; lvl < 30; lvl++) {
			// if (WebMercatorCellSizesf[lvl] > res)
			if (res > WebMercatorCellSizesf[lvl])
				break;
		}

		for (int chosenLvl = lvl; chosenLvl >=0; chosenLvl--) {
			if (env.haveLevel(chosenLvl)) {
				// fmt::print(" - findLvlForRes {}, choose {} downward from {}\n", res, chosenLvl, lvl);
				return chosenLvl;
			}
		}

		for (int chosenLvl = lvl; chosenLvl <30; chosenLvl++) {
			if (env.haveLevel(chosenLvl)) {
				// fmt::print(" - findLvlForRes {}, choose {} upward from {}\n", res, chosenLvl, lvl);
				return chosenLvl;
			}
		}

		throw NoValidLevelError(res, lvl);
	}





	FlatReaderCached::FlatReaderCached(const std::string& path, const EnvOptions& opts)
		: FlatReader(path, opts), cache(64) {

	}

	cv::Mat FlatReaderCached::getTile(uint64_t tile, int channels) {
		cv::Mat out;

		if (openOpts.cache) {
			if (cache.get(out, tile)) {
				out = FlatReader::getTile(tile, channels);
				cache.set(tile, out);
			}
		} else {
			out = FlatReader::getTile(tile, channels);
		}
		return out;
	}

	bool FlatReaderCached::getTile(cv::Mat& out, uint64_t tile, int channels) {
		if (openOpts.cache) {
			if (cache.get(out, tile)) {
				bool stat = FlatReader::getTile(out, tile, channels);
				if (stat) return stat;

				cache.set(tile, out);
				return stat;
			}
		} else {
			bool stat = FlatReader::getTile(out, tile, channels);
			if (stat) return stat;
		}
		return false;
	}


	cv::Mat FlatReaderCached::getTlbr(uint64_t lvl, uint32_t tlbr[4], int channels) {
		uint64_t h = tlbr[3] - tlbr[1];
		uint64_t w = tlbr[2] - tlbr[0];
		auto cvType = get_cv_type_from_channels(channels, isTerrain());

		cv::Mat out(tileSize*h,tileSize*w,cvType);

		for (int y=0; y<h; y++)
		for (int x=0; x<w; x++) {
			BlockCoordinate bc(lvl, tlbr[1]+y, tlbr[0]+x);

			// cv::Mat tile = this->getTile(bc.c, channels);
			cv::Mat tile = FlatReader::getTile(bc.c, channels);
			// fmt::print(" - getTile :: {} {} {} | {} ===> {}x{}c{}\n", bc.z(), bc.y(), bc.x(), bc.c, tile.rows,tile.cols,tile.channels());

			int yy = h-1-y;

			if (tile.empty()) {
				out(cv::Rect{x*tileSize,yy*tileSize,tileSize,tileSize}) = cv::Scalar{0};
			} else {
				// fmt::print(" - copy to {} {}, {} {} c{}\n", x*tileSize, yy*tileSize, tileSize,tileSize,out.channels());
				// out(cv::Rect{x*tileSize,y*tileSize,tileSize,tileSize}) = tile;
				tile.copyTo(out(cv::Rect{x*tileSize,yy*tileSize,tileSize,tileSize}));
			}
		}

		return out;
	}


		//WARNING: Lightly test
		//FIXME: Test me
	cv::Mat FlatReaderCached::rasterIo(const double wmTlbr[4], int w, int h, int c) {
		// Determine optimal level to sample from.
		// Determine integer tlbr on that level.
		// getTlbr() to get those tiles.
		// Compute affine transform to get the output aabb.
		// Warp image sampled image.

		auto meter_w = (wmTlbr[2] - wmTlbr[0]);
		// float meters_per_pixel = meter_w / w;
		// float res = meters_per_pixel * tileSize;
		// NOTE: Test this
		float res = meter_w * (tileSize / static_cast<float>(w));

		uint32_t lvl = find_level_for_mpp(res);
		uint32_t iwmTlbr[4];
		double sampledWmTlbr[4];

		dwm_to_iwm(iwmTlbr, wmTlbr, lvl);
		iwm_to_dwm(sampledWmTlbr, iwmTlbr, lvl);
		// uint32_t iwmTlbrPlusOne[4] = {iwmTlbr[0],iwmTlbr[1], 1+iwmTlbr[2],1+iwmTlbr[3]};
		// iwm_to_dwm(sampledWmTlbr, iwmTlbrPlusOne, lvl);

		int iwm_w = iwmTlbr[2] - iwmTlbr[0];
		int iwm_h = iwmTlbr[3] - iwmTlbr[1];
		int n_tiles = iwm_w * iwm_h;

		// fmt::print(" - chosen would have width {} for asked width {}\n", iwm_w*256, w);

		if (n_tiles > maxRasterIoTiles) {
			throw SampleTooLargeError{static_cast<uint32_t>(iwm_w), static_cast<uint32_t>(iwm_h)};
		}

		cv::Mat sampledImg = getTlbr(lvl, iwmTlbr, c);
		if (sampledImg.empty()) return cv::Mat();


		double sampledWmW = sampledWmTlbr[2] - sampledWmTlbr[0];
		double sampledWmH = sampledWmTlbr[3] - sampledWmTlbr[1];
		double sampledW = tileSize * iwm_w;
		double sampledH = tileSize * iwm_h;
		double dw=w, dh=h;
		// double queryW = wmTlbr[2] - wmTlbr[0];
		// double queryH = wmTlbr[3] - wmTlbr[1];

		double pts1[] = {
			// 0, 0,
			// sampledW, 0,
			// sampledW, sampledH,
			// 0, sampledH,
			// sampledW, sampledH,
			// sampledW, 0,
			0, dh,
			dw, dh,
			dw, 0,
		};
		double pts2[] = {
			// (wmTlbr[0]-sampledWmTlbr[0]) * (dw/sampledWmW), (wmTlbr[1]-sampledWmTlbr[1]) * (dh/sampledWmH),
			// (wmTlbr[2]-sampledWmTlbr[0]) * (dw/sampledWmW), (wmTlbr[1]-sampledWmTlbr[1]) * (dh/sampledWmH),
			// (wmTlbr[2]-sampledWmTlbr[0]) * (dw/sampledWmW), (wmTlbr[3]-sampledWmTlbr[1]) * (dh/sampledWmH),
			(wmTlbr[0]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[1]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			(wmTlbr[2]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[1]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			(wmTlbr[2]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[3]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			// (wmTlbr[0]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[1]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			// (wmTlbr[2]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[1]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			// (wmTlbr[2]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[3]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
		};

		float pts1f[6];
		float pts2f[6];
		for (int i=0; i<6; i++) pts1f[i] = static_cast<float>(pts1[i]);
		for (int i=0; i<6; i++) pts2f[i] = static_cast<float>(pts2[i]);

		cv::Mat pts1_(3,2,CV_32FC1,pts1f);
		cv::Mat pts2_(3,2,CV_32FC1,pts2f);
		// cv::Mat A = cv::getAffineTransform(pts1_, pts2_);
		cv::Mat A = cv::getAffineTransform(pts2_, pts1_);
		cv::Mat out;

		/*
		fmt::print(" - chosenlvl:: {}\n", lvl);
		fmt::print(" - ask tlbr :: {} {} {} {}\n", wmTlbr[0], wmTlbr[1], wmTlbr[2], wmTlbr[3]);
		fmt::print(" - iwm tlbr :: {} {} {} {}\n", iwmTlbr[0], iwmTlbr[1], iwmTlbr[2], iwmTlbr[3]);
		fmt::print(" - sam tlbr :: {} {} {} {} => {} {}\n", sampledWmTlbr[0], sampledWmTlbr[1], sampledWmTlbr[2], sampledWmTlbr[3], w,h);
		std::cout << " - warping with A:\n" << A <<"\n";
		std::cout << " - From pts:\n" << pts1_ << "\n" << pts2_ <<"\n";
		*/

		cv::warpAffine(sampledImg, out, A, cv::Size{w,h});
		// cv::resize(sampledImg, out, cv::Size{w,h});
		return out;
		// return sampledImg;
	}

	bool FlatReaderCached::rasterIo(cv::Mat out, const double wmTlbr[4]) {
		auto w = out.cols;
		auto h = out.rows;

		auto meter_w = (wmTlbr[2] - wmTlbr[0]);
		float res = meter_w * (tileSize / static_cast<float>(w));

		uint32_t lvl = find_level_for_mpp(res);

		uint32_t iwmTlbr[4];
		double sampledWmTlbr[4];

		dwm_to_iwm(iwmTlbr, wmTlbr, lvl);
		iwm_to_dwm(sampledWmTlbr, iwmTlbr, lvl);
		// uint32_t iwmTlbrPlusOne[4] = {iwmTlbr[0],iwmTlbr[1], 1+iwmTlbr[2],1+iwmTlbr[3]};
		// iwm_to_dwm(sampledWmTlbr, iwmTlbrPlusOne, lvl);

		int iwm_w = iwmTlbr[2] - iwmTlbr[0];
		int iwm_h = iwmTlbr[3] - iwmTlbr[1];
		int n_tiles = iwm_w * iwm_h;

		// fmt::print(" - chosen would have width {} for asked width {}\n", iwm_w*256, w);

		// WARNING: This allows a maximum size of e.g. 4096^2 pixels.
		if (n_tiles > maxRasterIoTiles) {
			throw SampleTooLargeError{static_cast<uint32_t>(iwm_w), static_cast<uint32_t>(iwm_h)};
		}

		// FIXME: Cache this image allocation as well.
		cv::Mat sampledImg = getTlbr(lvl, iwmTlbr, out.channels());
		if (sampledImg.empty()) return true;

		double sampledWmW = sampledWmTlbr[2] - sampledWmTlbr[0];
		double sampledWmH = sampledWmTlbr[3] - sampledWmTlbr[1];
		double sampledW = tileSize * iwm_w;
		double sampledH = tileSize * iwm_h;
		double dw=w, dh=h;
		// double queryW = wmTlbr[2] - wmTlbr[0];
		// double queryH = wmTlbr[3] - wmTlbr[1];

		double pts1[] = {
			0, dh,
			dw, dh,
			dw, 0,
		};
		double pts2[] = {
			(wmTlbr[0]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[1]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			(wmTlbr[2]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[1]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
			(wmTlbr[2]-sampledWmTlbr[0]) * (sampledW/sampledWmW), sampledH-(wmTlbr[3]-sampledWmTlbr[1]) * (sampledH/sampledWmH),
		};

		float pts1f[6];
		float pts2f[6];
		for (int i=0; i<6; i++) pts1f[i] = static_cast<float>(pts1[i]);
		for (int i=0; i<6; i++) pts2f[i] = static_cast<float>(pts2[i]);

		cv::Mat pts1_(3,2,CV_32FC1,pts1f);
		cv::Mat pts2_(3,2,CV_32FC1,pts2f);
		cv::Mat A = cv::getAffineTransform(pts2_, pts1_);

		cv::warpAffine(sampledImg, out, A, cv::Size{w,h});

		return false;
	}


}
