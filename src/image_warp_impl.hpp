//
// This file is included only in image.cc, and with correct headers and context.
//

namespace {


// Generate code for each combination of scalar type and channels.
// The correct function is dispatched in the Image member function.
template <class T, int C> void my_warpAffine(Image& out, const Image& in, const float H[6]);
template <class T, int C> void my_warpPerspective(Image& out, const Image& in, const float H[6]);

// Contains a type that is the next highest integer width,
// and a value that represents a safe value used for bilinear interpolation
template <class A> struct RaiseOnce;
template <> struct RaiseOnce<uint8_t> { using type = uint16_t; type res = 64; };
template <> struct RaiseOnce<uint16_t> { using type = uint32_t; type res = 64; };
template <> struct RaiseOnce<uint32_t> { using type = uint64_t; type res = 64; };


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


/*
// Specialize the one channel case, to make loop optims easier for compiler
// Probably doesn't help.
template <> void my_warpAffine<uint8_t, 1>(Image& out, const Image& in, const float H[6]) {
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
*/

template <class T, int C>
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
		using Scalar = typename RaiseOnce<T>::type;
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
template <class T, int C>
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
		using Scalar = typename RaiseOnce<T>::type;
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
