#pragma once

#include <opencv2/core.hpp>
#include <array>

#include "flat_env.h"
#include "frast2/detail/data_structures.hpp"

#include "codec.h"

namespace cv {
	class Mat;
};

namespace frast {

	void dwm_to_iwm(uint32_t iwmTlbr[4], const double wmTlbr[4], int lvl);
	void iwm_to_dwm(double dwmTlbr[4], const uint32_t iwmTlbr[4], int lvl);


	class FlatReader {
		public:
			FlatReader(const std::string& path, const EnvOptions& opts);

			static constexpr int tileSize=256;
			static constexpr int logTileSize=8;

			int64_t determineTlbr(uint32_t tlbr[4]);
			int64_t determineTlbrOnLevel(uint32_t tlbr[4], int lvl);
			std::vector<std::array<double,4>> computeRegionsOnDeepestLevel();


			cv::Mat getTile(uint64_t tile, int channels);
			bool getTile(cv::Mat& out, uint64_t tile, int channels);

			bool tileExists(uint64_t tile);

			void refreshMemMap();

			int levelSize(int lvl);

			FlatEnvironment env;


			inline bool isTerrain() const { return env.isTerrain(); }
			inline void setMaxRasterIoTiles(int n) { maxRasterIoTiles = n; }

		protected:

			std::string openPath;
			EnvOptions openOpts;

			int determineDeepeseLevel();

			int find_level_for_mpp(float res);

			int maxRasterIoTiles = 256;
	};

	class FlatReaderCached : public FlatReader {
		public:
			FlatReaderCached(const std::string& path, const EnvOptions& opts);

			cv::Mat getTile(uint64_t tile, int channels);
			bool getTile(cv::Mat& out, uint64_t tile, int channels);

			cv::Mat getTlbr(uint64_t lvl, uint32_t tlbr[4], int channels);

			cv::Mat rasterIo(const double tlbr[4], int w, int h, int c);
			bool rasterIo(cv::Mat out, const double tlbr[4]); // false on success

		private:

			LruCache<uint64_t,cv::Mat> cache;



	};



}
