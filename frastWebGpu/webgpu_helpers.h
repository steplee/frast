#pragma once

#define WEBGPU_BACKEND_DAWN

#include "webgpu/webgpu.hpp"

namespace frastwgpu {

struct Context {
	Context();
	wgpu::Instance instance;
}

}
