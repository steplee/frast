#include "codec.h"

#include "codec_terrain.hpp"
// #include "codec_stb.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {
	// constexpr bool USE_STB = true;

	inline bool use_stb(uint8_t option) {
		// WARNING: Right now stb is disabled!
		assert(option == 0);
		return false;
	}

}

namespace frast {

	// WARNING: This calls malloc(), and the user must then free memory with free()
	//         [This is because this function is typically used with ThreadPool]
	Value encodeValue(const cv::Mat& img, bool isTerrain, uint8_t option) {
		assert (not img.empty());


		if (isTerrain) {
			assert(img.channels() == 1);
			assert(img.type() == CV_16UC1);
			return encode_terrain_2x8(img);
		} else {

			if (use_stb(option)) {
				assert(false);
				/*
				std::vector<uint8_t> buf;
				my_write_jpg_stb(buf, img);
				Value v;
				// fmt::print(" - Writing {} bytes:", buf.size()); for (int i=0; i<20; i++) fmt::print(" {:0x}", buf[i]); fmt::print("\n");
				v.value = malloc(buf.size());
				v.len = buf.size();
				memcpy(v.value, buf.data(), buf.size());
				return v;
				*/
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
	}

	cv::Mat decodeValue(const Value& val, int channels, bool isTerrain, uint8_t option) {
		if (val.value == nullptr) return cv::Mat{};

		if (isTerrain) {
			assert (channels == 1);
			return decode_terrain_2x8(val);
		} else {
			assert(channels == 1 or channels == 3 or channels == 4);

			if (use_stb(option)) {
				assert(false);
				/*
				int w,h,c;
				uint8_t* mem = my_load_from_memory((uint8_t*)val.value, val.len, &w,&h,&c, channels);
				cv::Mat out (w,h, CV_8UC3); // FIXME: correct type
				memcpy(out.data, mem, out.total() * out.elemSize());
				free(mem);
				return out;
				*/

			} else {
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

	bool decodeValue(cv::Mat& out, const Value& val, int channels, bool isTerrain, uint8_t option) {
		if (val.value == nullptr) return true;

		if (isTerrain) {
			assert (channels == 1);
			return decode_terrain_2x8(out,val);
		} else {
			assert(channels == 1 or channels == 3 or channels == 4);

			if (use_stb(option)) {
				assert(false);
				/*
				int w,h,c;
				uint8_t* mem = my_load_from_memory((uint8_t*)val.value, val.len, &w,&h,&c, channels);
				out.create(w,h, CV_8UC3); // FIXME: correct type
				memcpy(out.data, mem, out.total() * out.elemSize());
				free(mem);
				return false;
				*/

			} else {
				bool grayscale = channels == 1;
				auto flags = grayscale ? 0 : cv::IMREAD_COLOR;

				cv::_InputArray buf((uint8_t*)val.value, val.len);
				cv::imdecode(buf, flags, &out);

				if (channels == 4 and out.channels() == 1) cv::cvtColor(out,out, cv::COLOR_GRAY2BGRA);
				if (channels == 4 and out.channels() == 3) cv::cvtColor(out,out, cv::COLOR_BGR2BGRA);

				return false;
			}
		}
	}


}
