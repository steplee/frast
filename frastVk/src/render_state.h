#pragma once

#include <cassert>
#include <vector>
#include <unordered_map>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>

struct MatrixStack {
	constexpr int NN = 10;
	alignas(16) float mvp[NN*16];
	int z = 0;

	inline MatrixStack() {
		for (int i=0; i<16*NN; i++) mvp[i] = (i%16) % 5 == 0;
	}

	void push(float* m) {
		float *mvp0 = mvp + z * 16;
		z++;
		assert(z < NN);
		float *mvp1 = mvp + z * 16;
		for (int i=0; i<4; i++)
		for (int j=0; j<4; j++) {
			float s = 0;
			for (int k=0; k<4; k++)
				s += mvp0[i*4+k] * m[k*4+j];
				//s += m[i*4+k] * mvp0[k*4+j];
			mvp1[i*4+j] = s;
		}
	}

	void pop() {
		z--;
		assert(z>=0);
	}
};

struct Shader {
};

struct ShaderSet {
	std::unordered_map<std::string, Shader> set;
};

struct RenderState {
	MatrixStack stk;
	ShaderSet shaders;
};
