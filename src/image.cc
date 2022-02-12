#include "image.h"
#include <iostream>
#include <cmath>
#include <cstdio>


// These two flags offer alternatives to OpenCV's warping and image encoding.
// I'm currently phasing opencv out, so I'll leave the old code for now.
#define USE_MY_WARP

//#define TURBOJPEG_QUALITY 95
//#define TURBOJPEG_QUALITY 100

#ifdef USE_TURBOJPEG
//#include <jpeglib.h>
#include <turbojpeg.h>
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#pragma clang diagnostic pop
#endif

// Use my implementation of a bilinear affine warp.
// Probably better to use cv::warpAffine anyway.
// Didn't use simd intrinsics and it's hard to tell if clang/llc auto-vectorized.
// But '-O3 -march=native -fopenmp' was able to compete with OpenCV's impl.
//#define USE_MY_WARP
// For my warp: whether to use floating point or int16_t bilinear weightings.
//              Probably faster, probably slightly worse quality.
#define INTEGER_MIXING 1

// TODO: Used fixed point arithmetic for bilinear weights as well.
//       Might need to use uint32_t instead of 16-bit.

#ifdef USE_MY_WARP
#include <Eigen/Core>
#include <Eigen/LU>
#ifndef _OPENMP
#error "if using my warp (USE_MY_WARP defined), you must pass -fopenmp"
//#include <omp.h>
#endif
#else
#include <opencv2/imgproc.hpp>
#endif




// Implemented below, either directly with turbojpeg or with opencv
bool encode_jpeg(EncodedImage& eimg, const Image& img);
bool decode_jpeg(Image& out, const EncodedImageRef& eimg);

bool encode_terrain_2x8(EncodedImage& eimg, const Image& img);
bool decode_terrain_2x8(Image& out, const EncodedImageRef& eimg);

bool decode(Image& out, const EncodedImageRef& eimg) {
	if (out.format == Image::Format::TERRAIN_2x8)
		return decode_terrain_2x8(out, eimg);
	else
		return decode_jpeg(out, eimg);
}
bool encode(EncodedImage& out, const Image& img) {
	if (img.format == Image::Format::TERRAIN_2x8)
		return encode_terrain_2x8(out, img);
	else
		return encode_jpeg(out, img);
}

#ifdef USE_TURBOJPEG
// Based on https://github.com/libjpeg-turbo/libjpeg-turbo/blob/c23672ce52ae53bd846b555439aa0a070b6d2c07/tjbench.c#L139
bool decode_jpeg(Image& out, const EncodedImageRef& eimg) {
    tjhandle handle = tjInitDecompress();

	long jpegSize = eimg.len;
	uint8_t* jpegBuf = (uint8_t*) eimg.data;
	uint8_t* destBuf = out.buffer;
	auto pitch = out.w * out.channels();

	//printf(" - Decode with channels %d\n", out.channels());
    int pf = out.channels() == 1 ? TJPF_GRAY : TJPF_RGB;
	int flags = 0;

	if (tjDecompress2(handle, jpegBuf, jpegSize,
				destBuf, out.w, pitch, out.h, pf,
				flags) == -1)
		throw std::runtime_error("executing tjDecompress2()");

	if (tjDestroy(handle) == -1) throw std::runtime_error("executing tjDestroy()");
	return false;
}
bool encode_jpeg(EncodedImage& out, const Image& img) {
    tjhandle handle = tjInitCompress();

    unsigned long jpegSize = tjBufSize(img.w, img.h, img.channels() == 3 ? TJSAMP_444 : TJSAMP_GRAY);
	uint8_t *jpegBuf = (unsigned char *) tjAlloc(jpegSize);

    if(handle == NULL)
    {
        const char *err = (const char *) tjGetErrorStr();
		std::cerr << "TJ Error: " << err << " UNABLE TO INIT TJ Compressor Object\n";
        return true;
    }
    int jpegQual = TURBOJPEG_QUALITY;
    int width = img.w;
    int height = img.h;
    int nbands = img.channels();
    int flags = 0;
    //unsigned char* jpegBuf = NULL;
    int pitch = width * nbands;
    int pixelFormat = TJPF_GRAY;
    int jpegSubsamp = TJSAMP_GRAY;
    if(nbands == 3)
    {
        pixelFormat = TJPF_RGB;
        jpegSubsamp = TJSAMP_411;
    }
	//printf(" - Encode with channels %d, %d %d\n", img.channels(), pixelFormat, jpegSubsamp);

    int tj_stat = tjCompress2( handle, img.buffer, width, pitch, height,
        pixelFormat, &(jpegBuf), &jpegSize, jpegSubsamp, jpegQual, flags);
    if(tj_stat != 0)
    {
        const char *err = (const char *) tjGetErrorStr();
		std::cerr << "TurboJPEG Error: " << err << " UNABLE TO COMPRESS JPEG IMAGE\n";
        tjDestroy(handle);
        handle = NULL;
        return true;
    }

	if (out.capacity() < jpegSize) out.reserve(jpegSize * 2);
	out.resize(jpegSize);
	memcpy(out.data(), jpegBuf, jpegSize);

    int tjstat = tjDestroy(handle); // should deallocate data buffer
	tjFree(jpegBuf);
    handle = 0;
	return false;
}
#else
bool decode_jpeg(Image& out, const EncodedImageRef& eimg) {

	cv::InputArray eimg_ { (uint8_t*) eimg.data, static_cast<int>(eimg.len) };

	if (out.w > 0 and out.h > 0) {
		auto old_c = out.channels();
		auto old_cv_type = old_c == 3 ? CV_8UC3 : old_c == 4 ? CV_8UC4 : CV_8U;
		//cv::Mat out_ { out.buffer, out.h, out.w, old_cv_type };
		cv::Mat out_ { out.h, out.w, old_cv_type, out.buffer };

		cv::imdecode(eimg_, cv::IMREAD_UNCHANGED, &out_);

		// TODO: Assert old whc = new whc

		return false;
	} else {
		cv::Mat mat = cv::imdecode(eimg_, cv::IMREAD_UNCHANGED);
		std::cout << " - decoded:\n " << mat << "\n";
		std::cout << " - decoded:\n " << mat.rows << " " << mat.cols << " " << mat.channels() << "\n";

		out.w = mat.cols;
		out.h = mat.rows;
		out.format = mat.channels() == 1 ? Image::Format::GRAY : mat.channels() == 3 ? Image::Format::RGB : Image::Format::RGBA;
		size_t nbytes = out.size();
		//out.buffer = (uint8_t*) malloc(nbytes);
		out.alloc();
		memcpy(out.buffer, mat.data, nbytes);

		return false;
	}
}
bool encode_jpeg(EncodedImage& out, const Image& img) {
	auto cv_type = img.format == Image::Format::GRAY ? CV_8UC1 : img.format == Image::Format::RGB ? CV_8UC3 : CV_8UC4;
	cv::Mat mat { img.h, img.w, cv_type, img.buffer };
	cv::imencode(".jpg", mat, out);
	return false;
}
#endif

#include "image_merge_impl.hpp"

#ifdef USE_MY_WARP

#include "image_warp_impl.hpp"

void Image::warpAffine(Image& out, const float H[6]) const {
	if (out.channels() == 1) my_warpAffine<uint8_t,1>(out, *this, H);
	else if (out.channels() == 3) my_warpAffine<uint8_t,3>(out, *this, H);
	else if (out.channels() == 4) my_warpAffine<uint8_t,4>(out, *this, H);
	else throw std::runtime_error(std::string{"Image::warpAffine() unsupported number channels "} + std::to_string(out.channels()));
}

void Image::warpPerspective(Image& out, float H[9]) const {
	if (H[8] != 1.0f)
		for (int i=0; i<7; i++) H[i] /= H[8];

	if (out.channels() == 1) my_warpPerspective<uint8_t,1>(out, *this, H);
	else if (out.channels() == 3) my_warpPerspective<uint8_t,3>(out, *this, H);
	else if (out.channels() == 4) my_warpPerspective<uint8_t,4>(out, *this, H);
	else throw std::runtime_error(std::string{"Image::warpPerspective() unsupported number channels "} + std::to_string(out.channels()));
}

#else

void Image::warpAffine(Image& out, const float H[6]) const {
	auto cv_type = out.channels() == 1 ? CV_8U : out.channels() == 3 ? CV_8UC3 : CV_8UC4;
	const cv::Mat refIn { h, w, cv_type, buffer };
	cv::Mat refOut { out.h, out.w, cv_type, out.buffer };
	const cv::Mat h { 2, 3, CV_32F, (void*)H };
	cv::warpAffine(refIn, refOut, h, cv::Size{out.w,out.h});
}

void Image::warpPerspective(Image& out, float H[9]) const {
	auto cv_type = out.channels() == 1 ? CV_8U : out.channels() == 3 ? CV_8UC3 : CV_8UC4;
	const cv::Mat refIn { h, w, cv_type, buffer };
	cv::Mat refOut { out.h, out.w, cv_type, out.buffer };
	const cv::Mat h { 3, 3, CV_32F, H };
	cv::warpPerspective(refIn, refOut, h, cv::Size{out.w,out.h});
}


#endif

// There is no cv implementation, no #if check
template <int C> void my_remapRemap(Image& out, const Image& in, const float* map, int mw, int mh);

void Image::remapRemap(Image& out, const float* map, int mapSizeW, int mapSizeH) const {
	if (out.channels() == 1) my_remapRemap<1>(out, *this, map, mapSizeW, mapSizeH);
	else if (out.channels() == 3) my_remapRemap<3>(out, *this, map, mapSizeW, mapSizeH);
	else if (out.channels() == 4) my_remapRemap<4>(out, *this, map, mapSizeW, mapSizeH);
	else throw std::runtime_error(std::string{"Image::remapReamp() unsupported number channels "} + std::to_string(out.channels()));
}

template <int C> void my_remapRemap(Image& out, const Image& in, const float* map, int mw, int mh) {
	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;
	const int istep = iw * C;
	const int ostep = ow * C;
	const int mstep = mw * 2;

	auto IDX_MAP = [mh,mw,mstep](int y, int x) {
		y = y < 0 ? 0 : y >= mh ? mh-1 : y;
		x = x < 0 ? 0 : x >= mw ? mw-1 : x;
		return y * mstep + x * 2;
	};

	auto IDX = [ih,iw,istep](int y, int x, int c) {
		y = y < 0 ? 0 : y >= ih ? ih-1 : y;
		x = x < 0 ? 0 : x >= iw ? iw-1 : x;
		return y * istep + x * C + c;
	};

	// This is the required interpolation range.
	// If grid is 8x8 and image is 256x256,
	// grid should be sampled in range [0, 6.9999]
	//const float fmw = mw - 1.f, fmh = mh - 1.f;
	const float fmw = mw - 1.f, fmh = mh - 1.f;
	const float fow = ow, foh = oh;


	//omp_set_num_threads(4);
	#pragma omp parallel for schedule(static,4) num_threads(4)
	for (int oy=0; oy<oh; oy++) {
	for (int ox=0; ox<ow; ox++) {
		// 1) Compute/sample map coord/weights
		// 2) Compute/sample pixel coord/weights

		float ax = (((float)ox) / fow) * fmw;
		float ay = (((float)oy) / foh) * fmh;
		float mx0 = ax - floorf(ax), my0 = ay - floorf(ay);

		float ix = 0, iy = 0;
		ix += map[IDX_MAP(ay  , ax  )  ] * (1.f-my0) * (1.f-mx0);
		ix += map[IDX_MAP(ay  , ax+1)  ] * (1.f-my0) * (    mx0);
		ix += map[IDX_MAP(ay+1, ax+1)  ] * (    my0) * (    mx0);
		ix += map[IDX_MAP(ay+1, ax  )  ] * (    my0) * (1.f-mx0);
		iy += map[IDX_MAP(ay  , ax  )+1] * (1.f-my0) * (1.f-mx0);
		iy += map[IDX_MAP(ay  , ax+1)+1] * (1.f-my0) * (    mx0);
		iy += map[IDX_MAP(ay+1, ax+1)+1] * (    my0) * (    mx0);
		iy += map[IDX_MAP(ay+1, ax  )+1] * (    my0) * (1.f-mx0);

		float mx = ix - floorf(ix), my = iy - floorf(iy);

#if INTEGER_MIXING
		using Scalar = uint16_t;
		using Vec = Scalar[C];
		Vec p = {0};
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+0) , (((int)ix)+0) , c)] * ((Scalar)(64.f * (1.f-my) * (1.f-mx)));
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+0) , (((int)ix)+1) , c)] * ((Scalar)(64.f * (1.f-my) * (    mx)));
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+1) , (((int)ix)+1) , c)] * ((Scalar)(64.f * (    my) * (    mx)));
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+1) , (((int)ix)+0) , c)] * ((Scalar)(64.f * (    my) * (1.f-mx)));
		for (int c=0; c<C; c++) out.buffer[oy*ostep+ox*C+c] = (uint8_t) (p[c] / 64);
#else
		using Scalar = float;
		using Vec = Scalar[C];
		Vec p = {0.f};
		for (int c=0; c<C; c++) p[c] += ((Scalar)in.buffer[IDX((((int)iy)+0) , (((int)ix)+0) , c)]) * ((Scalar)((1.f-my) * (1.f-mx)));
		for (int c=0; c<C; c++) p[c] += ((Scalar)in.buffer[IDX((((int)iy)+0) , (((int)ix)+1) , c)]) * ((Scalar)((1.f-my) * (    mx)));
		for (int c=0; c<C; c++) p[c] += ((Scalar)in.buffer[IDX((((int)iy)+1) , (((int)ix)+1) , c)]) * ((Scalar)((    my) * (    mx)));
		for (int c=0; c<C; c++) p[c] += ((Scalar)in.buffer[IDX((((int)iy)+1) , (((int)ix)+0) , c)]) * ((Scalar)((    my) * (1.f-mx)));
		for (int c=0; c<C; c++) out.buffer[oy*ostep+ox*C+c] = (uint8_t) (p[c]);
#endif
	}
	}
}







template <int C> static void makeGray_(Image& out, const Image& in) {
	int w = in.w, h = in.h;
	for (int y=0; y<h; y++)
	for (int x=0; x<w; x++) {
		uint16_t acc = 0;
		for (int c=0; c<C; c++) acc += in.buffer[y*w*C+x*C+c];
		out.buffer[y*w+x] = static_cast<uint8_t>(acc / C);
	}
}
void Image::makeGray(Image& out) const {
	if      (channels() == 1) memcpy(out.buffer, buffer, size());
	else if (channels() == 3) makeGray_<3>(out, *this);
	else if (channels() == 4) makeGray_<4>(out, *this);
	else throw std::runtime_error("invalid # channels");
}







// Terrain codec
bool encode_terrain_2x8(EncodedImage& eimg, const Image& img) {
	return false;
}
bool decode_terrain_2x8(Image& out, const EncodedImageRef& eimg) {
	return false;
}
