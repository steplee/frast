#pragma once

#define INTEGER_MIXING_HALFSCALE

namespace {

template <class T, int Cout, int Cin>
void my_halfscale(Image& out, const Image& in) {


	const int oh = out.h, ow = out.w;
	const int ih = in.h, iw = in.w;
	const int istep = iw * Cin;
	const int ostep = ow * Cout;
	const T* ibuf = (const T*) in.buffer;
	T* obuf = (T*) out.buffer;

	auto IDX = [ih,iw,istep](int y_, int x_, int c) {
		int y=y_, x=x_;
		y = y < 0 ? 0 : y >= ih ? ih-1 : y;
		x = x < 0 ? 0 : x >= iw ? iw-1 : x;
		auto out = y * istep + x * Cin + c;
		//printf("IDX %d %d -> %d %d\n", y_,x_,y,x);
		return out;
	};


	//omp_set_num_threads(4);
	#pragma omp parallel for schedule(static,4) num_threads(4)
	for (int oy=0; oy<oh; oy++) {
	for (int ox=0; ox<ow; ox++) {
#ifdef INTEGER_MIXING_HALFSCALE
		using Scalar = typename RaiseOnce<T>::type;
		using Vec = Scalar[Cin];
		constexpr float f_res = static_cast<float>(RaiseOnce<T>::res);
		/*
		Vec p = {0};
		for (int c=0; c<Cin; c++) p[c] += ibuf[IDX((((int)iy)+0) , (((int)ix)+0) , c)] * ((Scalar)(f_res * (1.f-my) * (1.f-mx)));
		for (int c=0; c<Cin; c++) p[c] += ibuf[IDX((((int)iy)+0) , (((int)ix)+1) , c)] * ((Scalar)(f_res * (1.f-my) * (    mx)));
		for (int c=0; c<Cin; c++) p[c] += ibuf[IDX((((int)iy)+1) , (((int)ix)+1) , c)] * ((Scalar)(f_res * (    my) * (    mx)));
		for (int c=0; c<Cin; c++) p[c] += ibuf[IDX((((int)iy)+1) , (((int)ix)+0) , c)] * ((Scalar)(f_res * (    my) * (1.f-mx)));
		for (int c=0; c<Cin; c++) obuf[oy*ostep+ox*Cout+c] = (T) (p[c] / RaiseOnce<T>::res);
		*/

		Vec p = {0};
		for (int c=0; c<Cin; c++) p[c] +=
			(static_cast<Scalar>(ibuf[istep*(oy*2  ) + (ox*2+1)*Cin + c]) * RaiseOnce<T>::res +
			 static_cast<Scalar>(ibuf[istep*(oy*2  ) + (ox*2  )*Cin + c]) * RaiseOnce<T>::res +
			 static_cast<Scalar>(ibuf[istep*(oy*2+1) + (ox*2  )*Cin + c]) * RaiseOnce<T>::res +
			 static_cast<Scalar>(ibuf[istep*(oy*2+1) + (ox*2+1)*Cin + c]) * RaiseOnce<T>::res ) / 4;

		for (int c=0; c<Cin; c++) obuf[oy*ostep+ox*Cout+c] = (T) (p[c] / RaiseOnce<T>::res);

#else
		assert(false);
#endif
	}
	}
}

}
