#pragma once

#include <opencv2/core.hpp>

#include "flat_env.h"
#include "detail/data_structures.hpp"

namespace cv {
	class Mat;
};

namespace frast {

	void dwm_to_iwm(uint32_t iwmTlbr[4], const double wmTlbr[4], int lvl);
	void iwm_to_dwm(double dwmTlbr[4], const uint32_t iwmTlbr[4], int lvl);

	class FlatReader {
		public:
			FlatReader(const std::string& path, const EnvOptions& opts);

			int64_t determineTlbr(uint32_t tlbr[4]);

			cv::Mat getTile(uint64_t tile, int channels);

			void refreshMemMap();

			FlatEnvironment env;

			static constexpr int tileSize=256;
			static constexpr int logTileSize=8;

		protected:

			bool isTerrain = false;

			std::string openPath;
			EnvOptions openOpts;

			int determineDeepeseLevel();

			int find_level_for_mpp(float res);
	};

	class FlatReaderCached : public FlatReader {
		public:
			FlatReaderCached(const std::string& path, const EnvOptions& opts);

			cv::Mat getTile(uint64_t tile, int channels);
			cv::Mat getTlbr(uint64_t lvl, uint32_t tlbr[4], int channels);

			cv::Mat rasterIo(double tlbr[4], int w, int h, int c);

		private:

			LruCache<uint64_t,cv::Mat> cache;



	};



}
