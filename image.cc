#include "image.h"
#include <iostream>
#include <cmath>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#pragma clang diagnostic pop


// Use my implementation of a bilinear affine warp.
// Probably better to use cv::warpAffine anyway.
// Didn't use simd intrinsics and it's hard to tell if clang/llc auto-vectorized.
// But '-O3 -march=native -fopenmp' was able to compete with OpenCV's impl.
#define USE_MY_WARP

#ifdef USE_MY_WARP
#ifndef _OPENMP
#error "if using my warp (USE_MY_WARP defined), you must pass -fopenmp"
#endif
//#include <omp.h>
#endif

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

template <int C> void my_warpAffine(Image& out, const Image& in, float H[6]);

template <> void my_warpAffine<1>(Image& out, const Image& in, float H[6]) {
	float iH[6];
	inv_3x2(iH,H);

	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;
	const int istep = iw;
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

		float p = 0.f;
		p += ((float)in.buffer[IDX((((int)iy)+0) , (((int)ix)+0))]) * ((float)((1.f-my) * (1.f-mx)));
		p += ((float)in.buffer[IDX((((int)iy)+0) , (((int)ix)+1))]) * ((float)((1.f-my) * (    mx)));
		p += ((float)in.buffer[IDX((((int)iy)+1) , (((int)ix)+1))]) * ((float)((    my) * (    mx)));
		p += ((float)in.buffer[IDX((((int)iy)+1) , (((int)ix)+0))]) * ((float)((    my) * (1.f-mx)));
		out.buffer[oy*istep+ox] = (uint8_t) (p);
	}
}

template <int C>
void my_warpAffine(Image& out, const Image& in, float H[6]) {
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

#if 0
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

void Image::warpAffine(Image& out, float H[6]) const {
#ifdef USE_MY_WARP
	if (out.channels() == 1) my_warpAffine<1>(out, *this, H);
	else if (out.channels() == 3) my_warpAffine<3>(out, *this, H);
	else if (out.channels() == 4) my_warpAffine<4>(out, *this, H);
	else throw std::runtime_error("Image::warpAffine() unsupported number channels " + out.channels());
#else
	const cv::Mat refIn { h, w, CV_8U, buffer };
	cv::Mat refOut { out.h, out.w, CV_8U, out.buffer };
	const cv::Mat h { 2, 3, CV_32F, H };
	cv::warpAffine(refIn, refOut, h, cv::Size{out.w,out.h});
#endif
}

