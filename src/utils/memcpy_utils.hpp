#pragma once

#include <fmt/core.h>

// Inteded for use only in db.cc

// Assumes channelStride = 1.
template <class T, int channels>
inline void memcpyStridedOutputFlatInput(T* dst, const T* src, size_t rowStride, size_t w, size_t h) {
	AtomicTimerMeasurement g(t_memcpyStrided);
	for (int y=0; y<h; y++)
	for (int x=0; x<w; x++)
	for (int c=0; c<channels; c++) {
		//dst[y*rowStride*channels + x*channels + c] = src[y*w*channels+x*channels+c];
		dst[y*rowStride*channels + x*channels + c] = *(src++);
	}
}

// Output is RGBA, input is Gray
// Alpha channel not touched.
template <class T>
void memcpyStridedOutputFlatInputReplicateRgbPadAlpha(T* dst, const T* src, size_t rowStride, size_t w, size_t h) {
	AtomicTimerMeasurement g(t_memcpyStrided);
	for (int y=0; y<h; y++)
	for (int x=0; x<w; x++) {
		dst[y*rowStride*4 + x*4 + 0] = *(src  );
		dst[y*rowStride*4 + x*4 + 1] = *(src  );
		dst[y*rowStride*4 + x*4 + 2] = *(src++);
		// dst[y*rowStride*4 + x*4 + 3] = 255;
	}
}

// Output is RGBA, input is RGB
// Alpha channel not touched.
template <class T>
void memcpyStridedOutputFlatInputPadAlpha(T* dst, const T* src, size_t rowStride, size_t w, size_t h) {
	AtomicTimerMeasurement g(t_memcpyStrided);
	for (int y=0; y<h; y++)
	for (int x=0; x<w; x++) {
		dst[y*rowStride*4 + x*4 + 0] = *(src++);
		dst[y*rowStride*4 + x*4 + 1] = *(src++);
		dst[y*rowStride*4 + x*4 + 2] = *(src++);
	}
}
