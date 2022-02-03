#include "image.h"
#include <iostream>
#include <cmath>
#include <cstdio>


// These two flags offer alternatives to OpenCV's warping and image encoding.
// I'm currently phasing opencv out, so I'll leave the old code for now.
#define USE_MY_WARP
#define USE_TURBOJPEG

#define TURBOJPEG_QUALITY 92
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

#ifdef USE_TURBOJPEG
// Based on https://github.com/libjpeg-turbo/libjpeg-turbo/blob/c23672ce52ae53bd846b555439aa0a070b6d2c07/tjbench.c#L139
bool decode(Image& out, const EncodedImageRef& eimg) {
    tjhandle handle = tjInitDecompress();

	long jpegSize = eimg.len;
	uint8_t* jpegBuf = (uint8_t*) eimg.data;
	uint8_t* destBuf = out.buffer;
	auto pitch = out.w * out.channels();

    int pf = out.channels() == 1 ? TJPF_GRAY : TJPF_RGB;
	int flags = 0;

	if (tjDecompress2(handle, jpegBuf, jpegSize,
				destBuf, out.w, pitch, out.h, pf,
				flags) == -1)
		throw std::runtime_error("executing tjDecompress2()");

	if (tjDestroy(handle) == -1) throw std::runtime_error("executing tjDestroy()");
	return false;
}
bool encode(EncodedImage& out, const Image& img) {
    tjhandle handle = tjInitCompress();

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
    unsigned char* jpegBuf = NULL;
    int pitch = width * nbands;
    int pixelFormat = TJPF_GRAY;
    int jpegSubsamp = TJSAMP_GRAY;
    if(nbands == 3)
    {
        pixelFormat = TJPF_RGB;
        jpegSubsamp = TJSAMP_411;
    }
    unsigned long jpegSize = 0;

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
    handle = 0;
	return false;
}
#else
bool decode(Image& out, const EncodedImageRef& eimg) {

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
bool encode(EncodedImage& out, const Image& img) {
	auto cv_type = img.format == Image::Format::GRAY ? CV_8UC1 : img.format == Image::Format::RGB ? CV_8UC3 : CV_8UC4;
	cv::Mat mat { img.h, img.w, cv_type, img.buffer };
	cv::imencode(".jpg", mat, out);
	return false;
}
#endif


// Should be optimized heavily with -O3.
// But could also hand-implement SSE/NEON intrinsics to ensure fast.
//
// TODO XXX: What about jpeg compression? Like zeros may be uncompressed to ones and we'd lose the nodata flag...
//           Perhaps check like [0,1,2] as nodata...
//
void Image::add_nodata__average_(const Image& other, uint16_t nodata) {
	int n = size();
	int wh = w*h;
	int c = channels();
	if (c == 1) {
		for (int i=0; i<wh; i++) {
			uint16_t a = static_cast<uint16_t>(buffer[i]);
			uint16_t b = static_cast<uint16_t>(other.buffer[i]);
			buffer[i] = static_cast<uint8_t>(
				(a == nodata) ? b : (b == nodata) ? a : (a+b)/2);
		}
	} else if (c == 3) {
		for (int i=0; i<wh; i++) {
			uint16_t a[3] = {
				static_cast<uint16_t>(buffer[i*3+0]),
				static_cast<uint16_t>(buffer[i*3+1]),
				static_cast<uint16_t>(buffer[i*3+2]) };
			uint16_t b[3] = {
				static_cast<uint16_t>(other.buffer[i*3+0]),
				static_cast<uint16_t>(other.buffer[i*3+1]),
				static_cast<uint16_t>(other.buffer[i*3+2]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata;
			buffer[i*3+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : (a[0]+b[0])/2);
			buffer[i*3+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : (a[1]+b[1])/2);
			buffer[i*3+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : (a[2]+b[2])/2);
		}
	} else if (c == 4) {
		for (int i=0; i<wh; i++) {
			uint16_t a[4] = {
				static_cast<uint16_t>(buffer[i*4+0]),
				static_cast<uint16_t>(buffer[i*4+1]),
				static_cast<uint16_t>(buffer[i*4+2]),
				static_cast<uint16_t>(buffer[i*4+3]), };
			uint16_t b[4] = {
				static_cast<uint16_t>(other.buffer[i*4+0]),
				static_cast<uint16_t>(other.buffer[i*4+1]),
				static_cast<uint16_t>(other.buffer[i*4+2]),
				static_cast<uint16_t>(other.buffer[i*4+3]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata and a[3] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata and b[3] == nodata;
			buffer[i*4+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : (a[0]+b[0])/2);
			buffer[i*4+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : (a[1]+b[1])/2);
			buffer[i*4+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : (a[2]+b[2])/2);
			buffer[i*4+3] = static_cast<uint8_t>(a_bad ? b[3] : b_bad ? a[3] : (a[3]+b[3])/2);
		}
	} else {
		throw std::runtime_error("not supported yet.");
	}
}
void Image::add_nodata__keep_(const Image& other, uint16_t nodata) {
	int n = size();
	int wh = w*h;
	int c = channels();
	if (c == 1) {
		for (int i=0; i<wh; i++) {
			uint16_t a = static_cast<uint16_t>(buffer[i]);
			uint16_t b = static_cast<uint16_t>(other.buffer[i]);
			buffer[i] = static_cast<uint8_t>(
				(a == nodata) ? b : (b == nodata) ? a : a);
		}
	} else if (c == 3) {
		for (int i=0; i<wh; i++) {
			uint16_t a[3] = {
				static_cast<uint16_t>(buffer[i*3+0]),
				static_cast<uint16_t>(buffer[i*3+1]),
				static_cast<uint16_t>(buffer[i*3+2]) };
			uint16_t b[3] = {
				static_cast<uint16_t>(other.buffer[i*3+0]),
				static_cast<uint16_t>(other.buffer[i*3+1]),
				static_cast<uint16_t>(other.buffer[i*3+2]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata;
			buffer[i*3+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : a[0]);
			buffer[i*3+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : a[1]);
			buffer[i*3+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : a[2]);
		}
	} else if (c == 4) {
		for (int i=0; i<wh; i++) {
			uint16_t a[4] = {
				static_cast<uint16_t>(buffer[i*4+0]),
				static_cast<uint16_t>(buffer[i*4+1]),
				static_cast<uint16_t>(buffer[i*4+2]),
				static_cast<uint16_t>(buffer[i*4+3]), };
			uint16_t b[4] = {
				static_cast<uint16_t>(other.buffer[i*4+0]),
				static_cast<uint16_t>(other.buffer[i*4+1]),
				static_cast<uint16_t>(other.buffer[i*4+2]),
				static_cast<uint16_t>(other.buffer[i*4+3]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata and a[3] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata and b[3] == nodata;
			buffer[i*4+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : a[0]);
			buffer[i*4+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : a[1]);
			buffer[i*4+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : a[2]);
			buffer[i*4+3] = static_cast<uint8_t>(a_bad ? b[3] : b_bad ? a[3] : a[3]);
		}
	} else {
		throw std::runtime_error("not supported yet.");
	}
}

#ifdef USE_MY_WARP
namespace {
inline void inv_3x2(float iH[6], const float H[6]) {
		float a=H[0*3+0], b=H[0*3+1], c=H[1*3+0], d=H[1*3+1];
		float det = std::fabs(a*d - b*c);
		float tx = H[0*3+2];
		float ty = H[1*3+2];
		a /= det; b /= det; c /= det; d /= det;
		iH[0*3+0] = d;
		iH[1*3+1] = a;
		iH[0*3+1] = -b;
		iH[1*3+0] = -c;
		iH[0*3+2] = -(iH[0*3+0] * tx + iH[0*3+1] * ty);
		iH[1*3+2] = -(iH[1*3+0] * tx + iH[1*3+1] * ty);
}
inline void inv_3x3(float iH_[9], const float H_[9]) {
	Eigen::Map<Eigen::Matrix<float,3,3,Eigen::RowMajor>> iH { iH_ };
	// Don't map H_, because it's returned by a cv function and may not be aligned.
	Eigen::Matrix<float,3,3,Eigen::RowMajor> H;
	for (int i=0; i<9; i++) H(i/3,i%3) = H_[i];

	iH = H.inverse();
}

template <int C> void my_warpAffine(Image& out, const Image& in, const float H[6]);

// Specialize the one channel case, to make loop optims easier for compiler
// Probably doesn't help.
template <> void my_warpAffine<1>(Image& out, const Image& in, const float H[6]) {
	float iH[6];
	inv_3x2(iH,H);

	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;
	const int istep = iw;
	const int ostep = ow;
	auto IDX = [ih,iw,istep](int y, int x) {
		y = y < 0 ? 0 : y >= ih ? ih-1 : y;
		x = x < 0 ? 0 : x >= iw ? iw-1 : x;
		return y * istep + x;
	};

	//omp_set_num_threads(4);
	#pragma omp parallel for schedule(static,4) num_threads(4)
	// VERY Interestingly, having one loop header is slower (up-to 2x).
	// Why? Is it the integer mod and div, or does it prevent some loop optimization?
	//for (int i=0; i<oh*ow; i++) {
		//int ox = i % ow, oy = i / ow;
	for (int oy=0; oy<oh; oy++)
	for (int ox=0; ox<ow; ox++) {
		float ix = iH[0*3+0] * ((float)ox) + iH[0*3+1] * ((float)oy) + iH[0*3+2];
		float iy = iH[1*3+0] * ((float)ox) + iH[1*3+1] * ((float)oy) + iH[1*3+2];
		float mx = ix - floorf(ix), my = iy - floorf(iy);

#if INTEGER_MIXING
		using Scalar = uint16_t;
		Scalar p = {0};
		p += in.buffer[IDX((((int)iy)+0) , (((int)ix)+0))] * ((Scalar)(64.f * (1.f-my) * (1.f-mx)));
		p += in.buffer[IDX((((int)iy)+0) , (((int)ix)+1))] * ((Scalar)(64.f * (1.f-my) * (    mx)));
		p += in.buffer[IDX((((int)iy)+1) , (((int)ix)+1))] * ((Scalar)(64.f * (    my) * (    mx)));
		p += in.buffer[IDX((((int)iy)+1) , (((int)ix)+0))] * ((Scalar)(64.f * (    my) * (1.f-mx)));
		out.buffer[oy*ostep+ox] = (uint8_t) (p / 64);
#else
		float p = 0.f;
		p += ((float)in.buffer[IDX((((int)iy)+0) , (((int)ix)+0))]) * ((float)((1.f-my) * (1.f-mx)));
		p += ((float)in.buffer[IDX((((int)iy)+0) , (((int)ix)+1))]) * ((float)((1.f-my) * (    mx)));
		p += ((float)in.buffer[IDX((((int)iy)+1) , (((int)ix)+1))]) * ((float)((    my) * (    mx)));
		p += ((float)in.buffer[IDX((((int)iy)+1) , (((int)ix)+0))]) * ((float)((    my) * (1.f-mx)));
		out.buffer[oy*ostep+ox] = (uint8_t) (p);
#endif
	}
}

template <int C>
void my_warpAffine(Image& out, const Image& in, const float H[6]) {
	float iH[6];
	inv_3x2(iH,H);


	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;
	const int istep = iw * C;
	const int ostep = ow * C;
	auto IDX = [ih,iw,istep](int y, int x, int c) {
		y = y < 0 ? 0 : y >= ih ? ih-1 : y;
		x = x < 0 ? 0 : x >= iw ? iw-1 : x;
		return y * istep + x * C + c;
	};

	//omp_set_num_threads(4);
	#pragma omp parallel for schedule(static,4) num_threads(4)
	for (int oy=0; oy<oh; oy++) {
	for (int ox=0; ox<ow; ox++) {
		float ix = iH[0*3+0] * ((float)ox) + iH[0*3+1] * ((float)oy) + iH[0*3+2];
		float iy = iH[1*3+0] * ((float)ox) + iH[1*3+1] * ((float)oy) + iH[1*3+2];
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

// Note: H[8] must be 1.0
template <int C>
void my_warpPerspective(Image& out, const Image& in, const float H[9]) {
	alignas(16) float iH[9];
	inv_3x3(iH,H);


	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;
	const int istep = iw * C;
	const int ostep = ow * C;
	auto IDX = [ih,iw,istep](int y, int x, int c) {
		y = y < 0 ? 0 : y >= ih ? ih-1 : y;
		x = x < 0 ? 0 : x >= iw ? iw-1 : x;
		return y * istep + x * C + c;
	};

	//omp_set_num_threads(4);
	#pragma omp parallel for schedule(static,4) num_threads(4)
	for (int oy=0; oy<oh; oy++) {
	for (int ox=0; ox<ow; ox++) {
		float iz = iH[2*3+0] * ((float)ox) + iH[2*3+1] * ((float)oy) + 1.f;
		float ix = (iH[0*3+0] * ((float)ox) + iH[0*3+1] * ((float)oy) + iH[0*3+2]) / iz;
		float iy = (iH[1*3+0] * ((float)ox) + iH[1*3+1] * ((float)oy) + iH[1*3+2]) / iz;
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

} // namespace
#endif

void Image::warpAffine(Image& out, const float H[6]) const {
#ifdef USE_MY_WARP
	if (out.channels() == 1) my_warpAffine<1>(out, *this, H);
	else if (out.channels() == 3) my_warpAffine<3>(out, *this, H);
	else if (out.channels() == 4) my_warpAffine<4>(out, *this, H);
	else throw std::runtime_error(std::string{"Image::warpAffine() unsupported number channels "} + std::to_string(out.channels()));
#else
	auto cv_type = out.channels() == 1 ? CV_8U : out.channels() == 3 ? CV_8UC3 : CV_8UC4;
	const cv::Mat refIn { h, w, cv_type, buffer };
	cv::Mat refOut { out.h, out.w, cv_type, out.buffer };
	const cv::Mat h { 2, 3, CV_32F, (void*)H };
	cv::warpAffine(refIn, refOut, h, cv::Size{out.w,out.h});
#endif
}

void Image::warpPerspective(Image& out, float H[9]) const {
#ifdef USE_MY_WARP
	if (H[8] != 1.0f)
		for (int i=0; i<7; i++) H[i] /= H[8];

	if (out.channels() == 1) my_warpPerspective<1>(out, *this, H);
	else if (out.channels() == 3) my_warpPerspective<3>(out, *this, H);
	else if (out.channels() == 4) my_warpPerspective<4>(out, *this, H);
	else throw std::runtime_error(std::string{"Image::warpPerspective() unsupported number channels "} + std::to_string(out.channels()));
#else
	auto cv_type = out.channels() == 1 ? CV_8U : out.channels() == 3 ? CV_8UC3 : CV_8UC4;
	const cv::Mat refIn { h, w, cv_type, buffer };
	cv::Mat refOut { out.h, out.w, cv_type, out.buffer };
	const cv::Mat h { 3, 3, CV_32F, H };
	cv::warpPerspective(refIn, refOut, h, cv::Size{out.w,out.h});
#endif
}




template <int C> void my_remapRemap(Image& out, const Image& in, const float* map, int mw, int mh);

// There is no cv implementation here anyway, so just write full func below.
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


