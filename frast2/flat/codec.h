#pragma once

#include "frast2/flat/flat_env.h"

namespace cv { class Mat; };


namespace frast {
	Value encodeValue(const cv::Mat& img, bool isTerrain);

	cv::Mat decodeValue(const Value& val, int outChannels, bool isTerrain);
	bool decodeValue(cv::Mat& out, const Value& val, int outChannels, bool isTerrain);
}
