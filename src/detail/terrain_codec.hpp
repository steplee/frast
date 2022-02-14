

#ifndef COMPRESS_TERRAIN

//
// Identity codec: store raw data with no compression.
//

bool encode_terrain_2x8(EncodedImage& eimg, const Image& img) {
	assert(img.format == Image::Format::TERRAIN_2x8);

	if (eimg.size() < img.size()) {
		eimg.reserve(img.size() * 2);
		eimg.resize(img.size());
	}

	memcpy(eimg.data(), img.buffer, img.size());

	return false;
}


bool decode_terrain_2x8(Image& out, const EncodedImageRef& eimg) {
	assert(out.format == Image::Format::TERRAIN_2x8);

	if (out.size() != eimg.len) {
		throw std::runtime_error(" - should match: " + std::to_string(out.size()) + " " + std::to_string(eimg.len));
	}
	memcpy(out.buffer, eimg.data, out.size());

	return false;
}

#else


//
// Deflate codec: store data using deflate algorithm
//

#include <zlib.h>

namespace {
	bool inflate_img(Image& out, uint8_t* data, int len) {
		z_stream strm;
		strm.zalloc   = Z_NULL;
		strm.zfree    = Z_NULL;
		strm.opaque   = Z_NULL;
		strm.avail_in = 0;
		strm.next_in  = Z_NULL;
		int ret       = inflateInit(&strm);
		if (ret != Z_OK) {
			inflateEnd(&strm);
			return true;
		}


		// strm.avail_in = in.size();
		// strm.next_in = (in.data());
		strm.avail_in = len;
		strm.next_in  = data;

		uint8_t* ptr = out.buffer;

		strm.avail_out = out.size();
		strm.next_out  = ptr;

		//printf(" - inflate header: %hhu %hhu %hhu %hhu\n", data[0], data[1], data[2], data[3]);
		//printf(" - inflate: %zu %p, %zu %p\n", strm.avail_in, strm.next_in, strm.avail_out, strm.next_out);

		do {

			ret = inflate(&strm, Z_NO_FLUSH);
			//ret = inflate(&strm, Z_FINISH);
			//printf(" - inflate ret: %d, avail_out %d, ptr %p\n", ret, strm.avail_out, ptr);
			assert(ret != Z_STREAM_ERROR); /* state not clobbered */
			switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR; /* and fall through */
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					// TODO XXX hitting here
					printf(" - zlib error %d, %s\n", ret, strm.msg);
					(void)inflateEnd(&strm);
					return true;
			}


		} while (ret != Z_STREAM_END);

		//out.resize(out.size() - strm.avail_out);

		//printf(" - -> inflate -- done: %zu %p, %zu %p\n", strm.avail_in, strm.next_in, strm.avail_out, strm.next_out);

		inflateEnd(&strm);
		return false;
	}

	bool deflate_img(EncodedImage& out, const Image& img) {
		z_stream strm;
		strm.zalloc   = Z_NULL;
		strm.zfree    = Z_NULL;
		strm.opaque   = Z_NULL;

		// For some reason directly using @out was broken, because resize() cleared the buffer to zero, when
		// I thought it was supposed to copy.
		// This was a quickfix: TODO: avoid the copy.
		std::vector<uint8_t> out0;
		out0.reserve(img.size());
		strm.avail_in = 0;
		strm.next_in  = 0;
		strm.avail_out  = 0;
		strm.next_out = 0;
		int ret       = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
		strm.avail_in = img.size();
		strm.next_in  = img.buffer;
		strm.avail_out  = out0.capacity();
		strm.next_out = out0.data();

		//printf(" - deflate: %zu %p, %zu %p\n", strm.avail_in, strm.next_in, strm.avail_out, strm.next_out);

		//int ret       = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_RLE, 15, 8);
		if (ret != Z_OK) {
			deflateEnd(&strm);
			return true;
		}

		do {
			ret = deflate(&strm, Z_FINISH);
			//printf(" - deflate ret: %d, avail_in %d\n", ret, strm.avail_in);

			if (ret == Z_DATA_ERROR or ret == Z_MEM_ERROR or ret == Z_BUF_ERROR) {
				printf(" - deflate err %d, %s\n", ret, strm.msg);
				throw std::runtime_error("bad");
				deflateEnd(&strm);
				return true;
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

		//out.resize(out0.size());
		out.resize(strm.total_out);
		for (int i=0; i<strm.total_out; i++) out[i] = out0[i];
		//printf(" - compressed %d -> %d, (vec %d, %d)\n", strm.total_in, strm.total_out, out.size(), out.capacity());

		if (ret != Z_STREAM_END) return true;
		ret = deflateEnd(&strm);
		//printf(" - deflate header: %hhu %hhu %hhu %hhu\n", out[0], out[1], out[2], out[3]);
		return false;
}

}


bool encode_terrain_2x8(EncodedImage& eimg, const Image& img) {
	assert(img.format == Image::Format::TERRAIN_2x8);

	if (eimg.capacity() < img.size()) {
		eimg.reserve(img.size() * 2);
	}

	return deflate_img(eimg, img);
}


bool decode_terrain_2x8(Image& out, const EncodedImageRef& eimg) {
	assert(out.format == Image::Format::TERRAIN_2x8);

	return inflate_img(out, (uint8_t*)eimg.data, eimg.len);
}

#endif
