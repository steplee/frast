#include "codec.h"

#include "terrain_codec.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {
}

namespace frast {

	// WARNING: This calls malloc(), and the user must then free memory with free()
	//         [This is because this function is typically used with ThreadPool]
	Value encodeValue(const cv::Mat& img, bool isTerrain) {
		assert (not img.empty());

		if (isTerrain) {
			assert(img.channels() == 1);
			assert(img.type() == CV_16UC1);
			return encode_terrain_2x8(img);
		} else {
			std::vector<uint8_t> buf;
			bool stat = cv::imencode(".jpg", img, buf);
			assert(stat);
			Value v;
			v.value = malloc(buf.size());
			v.len = buf.size();
			memcpy(v.value, buf.data(), buf.size());
			return v;
		}
	}

	cv::Mat decodeValue(const Value& val, int channels, bool isTerrain) {
		if (val.value == nullptr) return cv::Mat{};

		if (isTerrain) {
			assert (channels == 1);
			return decode_terrain_2x8(val);
		} else {
			assert(channels == 1 or channels == 3 or channels == 4);

			bool grayscale = channels == 1;
			auto flags = grayscale ? 0 : cv::IMREAD_COLOR;

			cv::_InputArray buf((uint8_t*)val.value, val.len);
			cv::Mat img = cv::imdecode(buf, flags);

			if (channels == 4 and img.channels() == 1) cv::cvtColor(img,img, cv::COLOR_GRAY2BGRA);
			if (channels == 4 and img.channels() == 3) cv::cvtColor(img,img, cv::COLOR_BGR2BGRA);

			return img;
		}
	}


}
