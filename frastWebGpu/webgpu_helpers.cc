#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu/webgpu.hpp"

#include "webgpu_helpers.h"


namespace frastwgpu {

namespace {
	wgpu::InstanceDescriptor getInstanceDescriptor() {
		wgpu::InstanceDescriptor desc = {};
		return desc;
	}
}

Context::Context()
	: instance(wgpu::createInstance(&getInstanceDescriptor()))
{
}


}
