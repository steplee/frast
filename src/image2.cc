#include <cmath>
#include <cstdio>
#include <iostream>
#include "image.h"

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
#include <Eigen/Geometry>
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

	long	 jpegSize = eimg.len;
	uint8_t* jpegBuf  = (uint8_t*)eimg.data;
	uint8_t* destBuf  = out.buffer;
	auto	 pitch	  = out.w * out.channels();

	// printf(" - Decode with channels %d\n", out.channels());
	int pf	  = out.channels() == 1 ? TJPF_GRAY : TJPF_RGB;
	int flags = 0;

	if (tjDecompress2(handle, jpegBuf, jpegSize, destBuf, out.w, pitch, out.h, pf, flags) == -1)
		throw std::runtime_error("executing tjDecompress2()");

	if (tjDestroy(handle) == -1) throw std::runtime_error("executing tjDestroy()");
	return false;
}
bool encode(EncodedImage& out, const Image& img) {
	tjhandle handle = tjInitCompress();

	unsigned long jpegSize = tjBufSize(img.w, img.h, img.channels() == 3 ? TJSAMP_444 : TJSAMP_GRAY);
	uint8_t*	  jpegBuf  = (unsigned char*)tjAlloc(jpegSize);

	if (handle == NULL) {
		const char* err = (const char*)tjGetErrorStr();
		std::cerr << "TJ Error: " << err << " UNABLE TO INIT TJ Compressor Object\n";
		return true;
	}
	int jpegQual = TURBOJPEG_QUALITY;
	int width	 = img.w;
	int height	 = img.h;
	int nbands	 = img.channels();
	int flags	 = 0;
	// unsigned char* jpegBuf = NULL;
	int pitch		= width * nbands;
	int pixelFormat = TJPF_GRAY;
	int jpegSubsamp = TJSAMP_GRAY;
	if (nbands == 3) {
		pixelFormat = TJPF_RGB;
		jpegSubsamp = TJSAMP_411;
	}
	// printf(" - Encode with channels %d, %d %d\n", img.channels(), pixelFormat, jpegSubsamp);

	int tj_stat = tjCompress2(handle, img.buffer, width, pitch, height, pixelFormat, &(jpegBuf), &jpegSize, jpegSubsamp,
							  jpegQual, flags);
	if (tj_stat != 0) {
		const char* err = (const char*)tjGetErrorStr();
		std::cerr << "TurboJPEG Error: " << err << " UNABLE TO COMPRESS JPEG IMAGE\n";
		tjDestroy(handle);
		handle = NULL;
		return true;
	}

	if (out.capacity() < jpegSize) out.reserve(jpegSize * 2);
	out.resize(jpegSize);
	memcpy(out.data(), jpegBuf, jpegSize);

	int tjstat = tjDestroy(handle);	 // should deallocate data buffer
	tjFree(jpegBuf);
	handle = 0;
	return false;
}
#else
bool decode(Image& out, const EncodedImageRef& eimg) {
	cv::InputArray eimg_{(uint8_t*)eimg.data, static_cast<int>(eimg.len)};

	if (out.w > 0 and out.h > 0) {
		auto old_c		 = out.channels();
		auto old_cv_type = old_c == 3 ? CV_8UC3 : old_c == 4 ? CV_8UC4 : CV_8U;
		// cv::Mat out_ { out.buffer, out.h, out.w, old_cv_type };
		cv::Mat out_{out.h, out.w, old_cv_type, out.buffer};

		cv::imdecode(eimg_, cv::IMREAD_UNCHANGED, &out_);

		// TODO: Assert old whc = new whc

		return false;
	} else {
		cv::Mat mat = cv::imdecode(eimg_, cv::IMREAD_UNCHANGED);
		std::cout << " - decoded:\n " << mat << "\n";
		std::cout << " - decoded:\n " << mat.rows << " " << mat.cols << " " << mat.channels() << "\n";

		out.w		  = mat.cols;
		out.h		  = mat.rows;
		out.format	  = mat.channels() == 1	  ? Image::Format::GRAY
						: mat.channels() == 3 ? Image::Format::RGB
											  : Image::Format::RGBA;
		size_t nbytes = out.size();
		// out.buffer = (uint8_t*) malloc(nbytes);
		out.alloc();
		memcpy(out.buffer, mat.data, nbytes);

		return false;
	}
}
bool encode(EncodedImage& out, const Image& img) {
	auto cv_type = img.format == Image::Format::GRAY ? CV_8UC1 : img.format == Image::Format::RGB ? CV_8UC3 : CV_8UC4;
	cv::Mat mat{img.h, img.w, cv_type, img.buffer};
	cv::imencode(".jpg", mat, out);
	return false;
}
#endif

// clang-format off

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
void Image::add_nodata__weighted_(const Image& other, uint32_t nodata_) {
	int32_t nodata = nodata_;
	int n = size();
	int wh = w*h;
	int c = channels();
	if (c == 1) {
		for (int i=0; i<wh; i++) {
			uint32_t a = static_cast<uint32_t>(buffer[i]);
			uint32_t b = static_cast<uint32_t>(other.buffer[i]);
			uint32_t w1 = 255 - std::abs((int)a-nodata);
			uint32_t w2 = 255 - std::abs((int)b-nodata);
			buffer[i] = static_cast<uint8_t>((a*w1 + b*w2) / (w1+w2));
		}
	} else if (c == 3) {
		for (int i=0; i<wh; i++) {

			uint32_t a[3] = {
				static_cast<uint32_t>(buffer[i*3+0]),
				static_cast<uint32_t>(buffer[i*3+1]),
				static_cast<uint32_t>(buffer[i*3+2]) };
			uint32_t b[3] = {
				static_cast<uint32_t>(other.buffer[i*3+0]),
				static_cast<uint32_t>(other.buffer[i*3+1]),
				static_cast<uint32_t>(other.buffer[i*3+2]) };
			uint32_t w1 = 1 + (std::abs((int)a[0]-nodata) + std::abs((int)a[1]-nodata) + std::abs((int)a[2]-nodata)) / 3;
			uint32_t w2 = 1 + (std::abs((int)b[0]-nodata) + std::abs((int)b[1]-nodata) + std::abs((int)b[2]-nodata)) / 3;

			//w1 = w1 * w1;
			//w2 = w2 * w2;
			buffer[i*3+0] = static_cast<uint8_t>((a[0]*w1 + b[0]*w2) / (w1+w2));
			buffer[i*3+1] = static_cast<uint8_t>((a[1]*w1 + b[1]*w2) / (w1+w2));
			buffer[i*3+2] = static_cast<uint8_t>((a[2]*w1 + b[2]*w2) / (w1+w2));
		}
	} else if (c == 4) {
		for (int i=0; i<wh; i++) {
			uint32_t a[4] = {
				static_cast<uint32_t>(buffer[i*4+0]),
				static_cast<uint32_t>(buffer[i*4+1]),
				static_cast<uint32_t>(buffer[i*4+2]),
				static_cast<uint32_t>(buffer[i*4+3]), };
			uint32_t b[4] = {
				static_cast<uint32_t>(other.buffer[i*4+0]),
				static_cast<uint32_t>(other.buffer[i*4+1]),
				static_cast<uint32_t>(other.buffer[i*4+2]),
				static_cast<uint32_t>(other.buffer[i*4+3]) };
			uint32_t w1 = 3*255 - std::abs((int)a[0]-nodata) - std::abs((int)a[1]-nodata) - std::abs((int)a[2]-nodata) - std::abs((int)a[3]-nodata);
			uint32_t w2 = 3*255 - std::abs((int)b[0]-nodata) - std::abs((int)b[1]-nodata) - std::abs((int)b[2]-nodata) - std::abs((int)b[3]-nodata);
			buffer[i*3+0] = static_cast<uint8_t>((a[0]*w1 + b[0]*w2) / (w1+w2));
			buffer[i*3+1] = static_cast<uint8_t>((a[1]*w1 + b[1]*w2) / (w1+w2));
			buffer[i*3+2] = static_cast<uint8_t>((a[2]*w1 + b[2]*w2) / (w1+w2));
			buffer[i*3+3] = static_cast<uint8_t>((a[3]*w1 + b[3]*w2) / (w1+w2));
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

template <int C>
void my_warpAffine(Image& out, const Image& in, const float H[6]) {
	float iH[6];
	inv_3x2(iH,H);

	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;

	using namespace Eigen;

	Map<const Array<uint8_t, C, -1>> A_(in.buffer, C, in.h*in.w);
	auto A = A_.template cast<uint32_t>();
	Map<Array<uint8_t, C, -1>> B(out.buffer, C, out.h*out.w);

	Map<const Matrix<float, 2, 3, RowMajor>> H_(iH);

	Matrix<float, 2, 1> inds0_(2, out.h*out.w);
	for (int oy=0; oy<oh; oy++) for (int ox=0; ox<ow; ox++) inds0_.col(oy*out.w+ox) << (float)ox, (float)oy;

	//Matrix<float, -1, 2> inds0 = (inds0_.rowwise().homogeneous() * H_.transpose()).rowwise().hnormalized();
	Matrix<float, 2,-1> inds0;
	Array<int, -1,1> inds = inds0.row(0).cast<int>() + inds0.row(1).cast<int>()*ow;


	ArrayXf my = inds0.row(1).array() - inds0.row(1).array().floor();
	ArrayXf mx = inds0.row(0).array() - inds0.row(0).array().floor();

	auto clampEdge = [ow,oh](auto &I) { return I.min(0).max(Array2i{ow,oh}); };

	auto a = A(all, clampEdge(inds))                * (64.f * (1.f-my) * (1.f-mx)).cast<uint32_t>();
	auto b = A(all, clampEdge(inds + Array2i{0,1})) * (64.f * (1.f-my) * (    mx)).cast<uint32_t>();
	auto c = A(all, clampEdge(inds + Array2i{1,1})) * (64.f * (    my) * (    mx)).cast<uint32_t>();
	auto d = A(all, clampEdge(inds + Array2i{1,0})) * (64.f * (    my) * (1.f-mx)).cast<uint32_t>();

	auto e = ((a+b+c+d) / 64).template cast<uint8_t>();
	for (int oy=0; oy<oh; oy++) for (int ox=0; ox<ow; ox++) B.col(oy*oh+ox) = e.col(oy*oh+ox);
}

int clamp_(int x, int hi) {
	return x < 0 ? 0 : x > hi-1 ? hi-1 : x;
}

// Note: H[8] must be 1.0
template <int C>
void my_warpPerspective(Image& out, const Image& in, const float H[9]) {
	alignas(16) float iH[9];
	inv_3x3(iH,H);

	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;

	using namespace Eigen;

	Map<const Array<uint8_t, C, -1>> A_(in.buffer, C, in.h*in.w);
	auto A = A_.template cast<uint32_t>();
	Map<Array<uint8_t, C, -1>> B(out.buffer, C, out.h*out.w);

	Map<const Matrix<float, 3, 3, RowMajor>> H_(iH);

	Matrix<float, 2, -1> inds0_(2, out.h*out.w);
	for (int oy=0; oy<oh; oy++) for (int ox=0; ox<ow; ox++) inds0_.col(oy*out.w+ox) << (float)ox, (float)oy;

	Matrix<float, 2, -1> inds0 = (H_ * inds0_.colwise().homogeneous()).colwise().hnormalized();

	Array<float,1,-1> my_ = inds0.row(1).array() - inds0.row(1).array().floor();
	Array<float,1,-1> mx_ = inds0.row(0).array() - inds0.row(0).array().floor();

#if 1
	Array<int, 2,-1> inds = inds0.cast<int>();

	auto clampEdge = [iw,ih](auto I) {
		//auto I2 = I.min(Array2i{ow,oh}).max(Array2i{ow,oh});
		Array<int,-1,1> I2(I.cols());
		for (int i=0; i<I.cols(); i++) I2(i) = clamp_(I(0,i), iw) + clamp_(I(1,i), ih) * iw;
		return I2;
	};

	auto a = A(all, clampEdge(inds               )).rowwise() * (64.f * (1.f-my_) * (1.f-mx_)).cast<uint32_t>();
	auto b = A(all, clampEdge(inds.colwise() + Array2i{1,0})).rowwise() * (64.f * (1.f-my_) * (    mx_)).cast<uint32_t>();
	auto c = A(all, clampEdge(inds.colwise() + Array2i{1,1})).rowwise() * (64.f * (    my_) * (    mx_)).cast<uint32_t>();
	auto d = A(all, clampEdge(inds.colwise() + Array2i{0,1})).rowwise() * (64.f * (    my_) * (1.f-mx_)).cast<uint32_t>();

	B = ((a+b+c+d) / 64).template cast<uint8_t>();
#else

	const int istep = iw * C;
	const int ostep = ow * C;
	auto IDX = [ih,iw,istep](int y, int x, int c) {
		y = y < 0 ? 0 : y >= ih ? ih-1 : y;
		x = x < 0 ? 0 : x >= iw ? iw-1 : x;
		return y * istep + x * C + c;
	};

	#pragma omp parallel for schedule(static) num_threads(4)
	for (int oy=0; oy<oh; oy++) {
	for (int ox=0; ox<ow; ox++) {
		float ix = inds0(0,oy*ow+ox);
		float iy = inds0(1,oy*ow+ox);
		float mx = mx_(oy*ow+ox);
		float my = my_(oy*ow+ox);

		using Scalar = uint16_t;
		using Vec = Scalar[C];
		Vec p = {0};
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+0) , (((int)ix)+0) , c)] * ((Scalar)(64.f * (1.f-my) * (1.f-mx)));
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+0) , (((int)ix)+1) , c)] * ((Scalar)(64.f * (1.f-my) * (    mx)));
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+1) , (((int)ix)+1) , c)] * ((Scalar)(64.f * (    my) * (    mx)));
		for (int c=0; c<C; c++) p[c] += in.buffer[IDX((((int)iy)+1) , (((int)ix)+0) , c)] * ((Scalar)(64.f * (    my) * (1.f-mx)));
		for (int c=0; c<C; c++) out.buffer[oy*ostep+ox*C+c] = (uint8_t) (p[c] / 64);
	}
	}
#endif

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
// clang-format on
