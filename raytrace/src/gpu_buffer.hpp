#pragma once

#include <cuda_runtime.h>
#include <cstdio>

template <class T>
struct GpuBuffer {
    inline      operator T*() const { return ptr; }
    inline void allocate(const size_t& n) {
        if (n > allocatedCnt) {
            if (ptr)
                cudaFree(ptr);

            //cudaMallocManaged(&ptr, sizeof(T) * n);
            cudaMalloc(&ptr, sizeof(T) * n);
            //printf(" GpuBuffer::allocated %x:%d prev %d\n", ptr, n*sizeof(T), sizeof(T)*allocatedCnt);

            cudaError_t err = cudaGetLastError();
            if (cudaSuccess != err) {
                fprintf(stderr, " %s:%i GpuBuffer::allocate FATAL ERROR, failed to allocate %d bytes: %zu %s", __FILE__,
                        __LINE__, (int)err, n*sizeof(T), cudaGetErrorString(err));
                exit(EXIT_FAILURE);
            }

            allocatedCnt = n;
        }
    }
    inline ~GpuBuffer() {
        //printf(" ~GpuBuffer %x:%d\n", ptr, allocatedCnt*sizeof(T));
        if (ptr)
            cudaFree(ptr);
        ptr = 0;
        allocatedCnt = 0;
    }

    inline GpuBuffer(GpuBuffer&& o) {
      o.ptr = ptr;
      o.allocatedCnt = allocatedCnt;
      ptr = nullptr;
      allocatedCnt = 0;
    }
    inline GpuBuffer& operator=(GpuBuffer&& o) {
      o.ptr = ptr;
      o.allocatedCnt = allocatedCnt;
      ptr = nullptr;
      allocatedCnt = 0;
      return o;
    }
    inline GpuBuffer() : ptr(nullptr), allocatedCnt(0) {}
    inline GpuBuffer(const size_t& n) : ptr(nullptr), allocatedCnt(0) { allocate(n); }

    size_t allocatedCnt = 0;
    T*     ptr          = nullptr;
};

