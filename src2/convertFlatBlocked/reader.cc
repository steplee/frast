#include "reader.h"

#include <opencv2/imgcodecs.hpp>

namespace frast {

	FlatReader::FlatReader(const std::string& path, const EnvOptions& opts)
		: openPath(path), openOpts(opts), env(path, opts) {

	}

	void FlatReader::refreshMemMap() {
		// env = std::move(FlatEnvironment{openPath,openOpts});
		env = (FlatEnvironment(openPath,openOpts));
	}


	int FlatReader::determineDeepeseLevel() {
		int deepest = -1;
		for (int i=0; i<26; i++) {
			if (env.meta()->levelSpecs[i].keysCapacity > 0) deepest = i;
		}
		return deepest;
	}

	int64_t FlatReader::determineTlbr(uint64_t tlbr[4]) {
		auto lvl = determineDeepeseLevel();

		for (int i=0; i<4; i++) tlbr[i] = 0;
		if (lvl == -1) return lvl;

		tlbr[0] = std::numeric_limits<uint64_t>::max();
		tlbr[1] = std::numeric_limits<uint64_t>::max();
		tlbr[2] = std::numeric_limits<uint64_t>::min();
		tlbr[3] = std::numeric_limits<uint64_t>::min();

		uint64_t* keys = env.getKeys(lvl);
		uint64_t n = env.meta()->levelSpecs[lvl].nitemsUsed();

		for (uint64_t i=0; i<n; i++) {
			BlockCoordinate bc(keys[i]);
			tlbr[0] = std::min(tlbr[0], bc.x());
			tlbr[1] = std::min(tlbr[1], bc.y());
			tlbr[2] = std::max(tlbr[2], bc.x());
			tlbr[3] = std::max(tlbr[3], bc.y());
		}

		return lvl;
	}

	cv::Mat FlatReader::getTile(uint64_t tile) {
		BlockCoordinate bc(tile);
		auto val = env.lookup(bc.z(), tile);

		if (val.value == nullptr) return cv::Mat{};

		// fmt::print(" - found tile {} :: {} {}\n", tile, val.value, val.len);

		bool grayscale = false;
		auto flags = grayscale ? 0 : cv::IMREAD_COLOR;

		// cv::Mat buf { 
		// std::vector<uint8_t> buf;
		cv::_InputArray buf((uint8_t*)val.value, val.len);
		// cv::Mat buf(val.len, 1, CV_8UC1, val.value);
		cv::Mat img = cv::imdecode(buf, flags);
		return img;
	}


}
