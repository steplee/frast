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

struct TileHeader {
	bool isOverview : 1 = false;
};

struct Image;
struct TileImage;
bool decode(TileImage& out, const EncodedImageRef& eimg);
bool decode(Image& out, const EncodedImageRef& eimg);
bool encode(EncodedImage& out, EncodedImage& tmp, const TileImage& img);


/*
 * You can 'view' other images, but it assumes the child lifetime does not out-last the view'ed parent.
 *
 * This really needs a stride/pitch member to allow fully-functional views.
 */
struct Image {
	int32_t w, h;
	enum class Format { RGB, RGBA, RGBN, GRAY } format;
	uint8_t *buffer = nullptr;
	bool ownBuffer = true;
	int32_t capacity = 0;

	// TODO: implement move operators

	// Value semantics (will copy entire buffer)
	inline Image(const Image& other) { copyFrom(other); }
	inline Image& operator=(const Image& other) { copyFrom(other); return *this; }

	inline ~Image() {
		if (buffer and ownBuffer) { free(buffer); buffer = 0; }
		buffer = 0;
		w = h = 0;
	}

	static inline Image view(int hh, int ww, Format fmt, uint8_t* buffer_) {
		Image out;
		out.ownBuffer = false;
		out.format = fmt;
		out.h = hh;
		out.w = ww;
		out.buffer = buffer_;
		return out;
	}
	static inline Image view(const Image& other) {
		Image out;
		out.w = other.w;
		out.h = other.h;
		out.format = other.format;
		out.buffer = other.buffer;
		out.ownBuffer = false;
		return out;
	}

	void copyFrom(const Image& other) {
		//if (other.buffer and buffer and ownBuffer and w == other.w and h == other.h and channels() == other.channels())
		if (other.buffer and buffer and capacity >= other.capacity)
			memcpy(buffer, other.buffer, size());
		else if (buffer and !ownBuffer and capacity < other.capacity)
			throw std::runtime_error("copyFrom() called on a viewed image that was too small.");
		else {
			if (buffer and ownBuffer) { free(buffer); buffer = 0; }
			if (other.w > 0 and other.h > 0) {
				w = other.w;
				h = other.h;
				format = other.format;
			}
			if (other.buffer) {
				ownBuffer = true;
				alloc();
				memcpy(buffer, other.buffer, size());
			}
		}
	}

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
		if (not ownBuffer) throw std::runtime_error("alloc() called on a viewed image!");
		if (buffer != nullptr) throw std::runtime_error("only alloc once!");
		//buffer = (uint8_t*) malloc(size());
		capacity = size();
		buffer = (uint8_t*) aligned_alloc(16, capacity);
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
	void warpAffine(Image& out, const float H[6]) const;
	// H is not const: it may be divided by H[8].
	void warpPerspective(Image& out, float H[9]) const;
};


struct TileImage : public Image, public TileHeader {

	inline TileImage() : Image() {}
	inline TileImage(int w, int h, int c) : Image(w,h,c) {}
	inline TileImage(int w, int h, Format f) : Image(w,h,f) {}
	inline TileImage(int w, int h, Format f, uint8_t* buf) : Image(w,h,f,buf) {}
	inline TileImage(int w, int h, int c, uint8_t* buf) : Image(w,h,c,buf) {}

};
//static_assert(sizeof(TileImage) == sizeof(TileHeader) + sizeof(Image));

