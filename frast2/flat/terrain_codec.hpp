#include "codec.h"
#include <opencv2/core.hpp>
#include <fmt/core.h>

#define FRAST_COMPRESS_TERRAIN=1

namespace frast {

#ifndef FRAST_COMPRESS_TERRAIN


//
// Identity codec: store raw data with no compression.
//

Value encode_terrain_2x8(const cv::Mat& img) {
	assert(img.type() == CV_16UC1);
	
	Value v;
	v.value = malloc(img.elemSize() * img.total());
	v.len = img.elemSize() * img.total();

	memcpy(v.value, img.data, v.len);

	return v;
}

cv::Mat decode_terrain_2x8(const Value& eimg) {
	cv::Mat out(256,256, CV_16UC1);

	assert(eimg.len == 256*256*2);
	memcpy(out.data, eimg.value, eimg.len);

	return out;
}

bool decode_terrain_2x8(cv::Mat& out, const Value& eimg) {
	// cv::Mat out(256,256, CV_16UC1);
	out.create(256,256,CV_16UC1);

	assert(eimg.len == 256*256*2);
	memcpy(out.data, eimg.value, eimg.len);

	return false;
}

#else

//
// Deflate codec: store data using deflate algorithm
//

#include <zlib.h>

namespace {
bool inflate_img(cv::Mat& out, uint8_t* data, int len) {
	z_stream strm;
	strm.zalloc	  = Z_NULL;
	strm.zfree	  = Z_NULL;
	strm.opaque	  = Z_NULL;
	strm.avail_in = 0;
	strm.next_in  = Z_NULL;
	int ret		  = inflateInit(&strm);
	if (ret != Z_OK) {
		inflateEnd(&strm);
		return true;
	}

	// strm.avail_in = in.size();
	// strm.next_in = (in.data());
	strm.avail_in = len;
	strm.next_in  = data;

	uint8_t* ptr = out.data;

	strm.avail_out = out.elemSize() * out.total();
	strm.next_out  = ptr;

	// printf(" - inflate header: %hhu %hhu %hhu %hhu\n", data[0], data[1], data[2], data[3]);
	// printf(" - inflate: %zu %p, %zu %p\n", strm.avail_in, strm.next_in, strm.avail_out, strm.next_out);

	do {
		ret = inflate(&strm, Z_NO_FLUSH);
		// ret = inflate(&strm, Z_FINISH);

		// printf(" - inflate ret: %d, avail_out %d, ptr %p\n", ret, strm.avail_out, ptr);
		assert(ret != Z_STREAM_ERROR); /* state not clobbered */
		switch (ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR; /* and fall through */
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
			case Z_BUF_ERROR:
				// TODO XXX hitting here
				printf(" - zlib error %d, %s avail_out %d avail_in %d\n", ret, strm.msg, strm.avail_out, strm.avail_in);
				(void)inflateEnd(&strm);
				assert(false);
				// return true;
		}

	} while (ret != Z_STREAM_END);

	// out.resize(out.size() - strm.avail_out);

	// printf(" - -> inflate -- done: %zu %p, %zu %p\n", strm.avail_in, strm.next_in, strm.avail_out, strm.next_out);

	inflateEnd(&strm);
	return false;
}

Value deflate_img(const cv::Mat& img) {
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree	= Z_NULL;
	strm.opaque = Z_NULL;

	// For some reason directly using @out was broken, because resize() cleared the buffer to zero, when
	// I thought it was supposed to copy.
	// This was a quickfix: TODO: avoid the copy.
	std::vector<uint8_t> out0;
	out0.reserve(img.elemSize() * img.total());
	strm.avail_in  = 0;
	strm.next_in   = 0;
	strm.avail_out = 0;
	strm.next_out  = 0;
	int ret		   = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
	strm.avail_in  = img.elemSize() * img.total();
	strm.next_in   = img.data;
	strm.avail_out = out0.capacity();
	strm.next_out  = out0.data();

	// printf(" - deflate: %zu %p, %zu %p\n", strm.avail_in, strm.next_in, strm.avail_out, strm.next_out);

	// int ret       = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_RLE, 15, 8);
	if (ret != Z_OK) {
		deflateEnd(&strm);
		return Value{};
	}

	do {
		ret = deflate(&strm, Z_FINISH);
		// printf(" - deflate ret: %d, avail_in %d\n", ret, strm.avail_in);

		if (ret == Z_DATA_ERROR or ret == Z_MEM_ERROR or ret == Z_BUF_ERROR) {
			printf(" - deflate err %d, %s\n", ret, strm.msg);
			throw std::runtime_error("bad");
			deflateEnd(&strm);
			return Value{};
		}

		// NOTE: Below shouldn't be needed, as output shouldn't be larger than input ever.
		// I think:
		// When avail_out is zero, that means zlib needs more space.
		// So increase+copy (using reserve()), then reset pointer.
		/*
		if (strm.avail_out == 0) {
			size_t off = strm.next_out - out.data();
			out.reserve(out.capacity() * 2);
			strm.avail_out = out.capacity() - off;
			strm.next_out = out.data() + off;
			// Should be rare.
			printf(" - avail_out 0, increasing to %zu, off %zu\n", out.capacity(), off);
		}
		*/
	} while (ret != Z_STREAM_END);

	Value v;
	v.value = malloc(strm.total_out);
	v.len = strm.total_out;
	memcpy(v.value, out0.data(), v.len);

	// fmt::print(" - deflated {} -> {} ({:.1f}%)\n", img.elemSize() * img.total(), strm.total_out, strm.total_out * 100.f / static_cast<float>(img.elemSize() * img.total()));


	if (ret != Z_STREAM_END) return Value{};
	ret = deflateEnd(&strm);

	// printf(" - deflate header: %hhu %hhu %hhu %hhu\n", out0[0], out0[1], out0[2], out0[3]);
	return v;
}

}  // namespace

Value encode_terrain_2x8(const cv::Mat& img) {
	return deflate_img(img);
}

cv::Mat decode_terrain_2x8(const Value& eimg) {
	cv::Mat out(256,256,CV_16UC1);
	bool stat = inflate_img(out, (uint8_t*)eimg.value, eimg.len);
	return out;
}

bool decode_terrain_2x8(cv::Mat& out, const Value& eimg) {
	out.create(256,256,CV_16UC1);
	bool stat = inflate_img(out, (uint8_t*)eimg.value, eimg.len);
	return false;
}

#endif

}
