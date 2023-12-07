#include <stdio.h>

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <emscripten/html5_webgpu.h>
#include <webgpu/webgpu_cpp.h>


int main() {
	printf("hello\n");

	auto deviceC = emscripten_webgpu_get_device();

	wgpu::Device device(deviceC);

	return 0;
}

