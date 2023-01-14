#pragma once

#include "flat/flat_env.h"

namespace cv { class Mat; };


namespace frast {
	Value encodeValue(const cv::Mat& img, bool isTerrain);

	cv::Mat decodeValue(const Value& val, int outChannels, bool isTerrain);
}
