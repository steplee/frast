#pragma once

#include <cstdio>
#include <cassert>

// WARNING: When I implement the cuda version, remove this
inline void cudaMalloc(void*,size_t n) { assert(false); }
inline void cudaFree(void*) { assert(false); }
// #include <cuda_runtime.h>


// A 1d or 2d cpu or device array.
// NOTE: just use cudaMallocManaged...

template <class T>
struct Buffer {
	T* ptr=nullptr;
	int capacity=0, w=0, h=0;
	bool isGpu = false;

	inline operator T*() const { return ptr; }

	inline Buffer() { }
	inline Buffer(bool gpu, int h, int w=1) {
		if (gpu)
			allocateGpu(gpu,h,w);
		else
			allocateCpu(gpu,h,w);
	}
	inline ~Buffer() {
		deallocate();
	}

	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;
	Buffer(Buffer&&) = default;
	Buffer& operator=(Buffer&&) = default;

	inline void deallocate() {
		if (isGpu) {
			if (ptr) cudaFree(ptr);
			ptr = 0;
			capacity = w=h = 0;
			isGpu = false;
		} else {
			if (ptr) free(ptr);
			ptr = 0;
			capacity = w=h = 0;
			isGpu = false;
		}
	}

	inline void allocateGpu(int h, int w=1) {
		int n = w*h;
		assert(n > 0);
		if (!isGpu) deallocate();
		if (n < capacity) {
			this->w = w;
			this->h = h;
			return;
		}

		if (n > capacity) deallocate();
		else return;

		assert(!ptr);
		cudaMalloc(&ptr, sizeof(T) * n);
		assert(ptr);

		capacity = n;
		this->w = w;
		this->h = h;
		isGpu = true;
	}

	inline void allocateCpu(int h, int w=1) {
		int n = w*h;
		assert(n > 0);
		if (isGpu) deallocate();
		if (n < capacity) {
			this->w = w;
			this->h = h;
			return;
		}

		if (n > capacity) deallocate();
		else return;

		assert(!ptr);
		ptr = (T*) malloc(sizeof(T) * n);
		assert(ptr);

		capacity = n;
		this->w = w;
		this->h = h;
		isGpu = false;
	}

	inline T& operator()(const int& y) {
		return ptr[y];
	}
	inline T& operator()(const int& y, const int& x) {
		return ptr[y*w+x];
	}
	inline const T& operator()(const int& y) const {
		return ptr[y];
	}
	inline const T& operator()(const int& y, const int& x) const {
		return ptr[y*w+x];
	}
};
