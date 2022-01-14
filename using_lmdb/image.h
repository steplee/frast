#pragma once

#include <atomic>
#include <mutex>
#include <cstdio>
#include <unistd.h>
#include <vector>
#include <cstddef>
#include <cstring>
#include <cstdlib>


//using EncodedImage = std::vector<std::byte>;
using EncodedImage = std::vector<uint8_t>;
struct EncodedImageRef {
	size_t len;
	uint8_t* data;
};


struct Image {
	int32_t w, h;
	enum class Format { RGB, RGBA, RGBN, GRAY } format;
	uint8_t *buffer = nullptr;
	bool ownBuffer = false;

	static Format c2format(int c) {
		switch (c) {
			case 1: { return Format::GRAY; }
			case 3: { return Format::RGB ; }
			case 4: { return Format::RGBA; }
		}
		throw std::runtime_error("bad channel count");
		return Format::GRAY;
	}

	inline Image() : w(0), h(0), buffer(nullptr), format(Format::GRAY) {}
	inline Image(int w, int h, int c) : w(w), h(h), buffer(nullptr), format(c2format(c)) {}
	inline Image(int w, int h, Format f) : w(w), h(h), format(f), buffer(nullptr) { }
	inline Image(int w, int h, Format f, uint8_t* buf) : w(w), h(h), format(f), buffer(buf) { }
	inline Image(int w, int h, int c, uint8_t* buf) : w(w), h(h), format(c2format(c)), buffer(buf) { }

	inline int32_t size() const {
		return w * h * channels();
	}
	inline int32_t channels() const {
		switch (format) {
			case Format::GRAY: return 1;
			case Format::RGBA: return 3;
			case Format::RGBN: return 4;
			default: return 3;
		}
	}

	inline bool alloc() {
		if (buffer != nullptr) throw std::runtime_error("only alloc once!");
		//buffer = (uint8_t*) malloc(size());
		buffer = (uint8_t*) aligned_alloc(16, size());
		ownBuffer = true;
		return buffer != nullptr;
	}
	inline bool calloc(uint8_t val = 0) {
		if (buffer == nullptr && !alloc()) return false;
		memset(buffer, val, size());
		return true;
	}

	// These operate in-place.
	// TODO: Support checking less-than or greater-than some nodata, rather than equality.
	void add_nodata__average_(const Image& other, uint16_t nodata=0);
	void add_nodata__keep_(const Image& other, uint16_t nodata=0);

	// Destination image should be allocated.
	void warpAffine(Image& out, float H[6]) const;
};


bool decode(Image& out, const EncodedImageRef& eimg);
bool encode(EncodedImage& out, const Image& img);

