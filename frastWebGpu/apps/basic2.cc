#include <stdio.h>

// From emscripten: https://github.com/emscripten-core/emscripten/tree/9429b4893c5e376e23e67d7007d2b948987445a0/system/include
#include <webgpu/webgpu.h>

// Also from emscripten
#include <GLFW/glfw3.h>

// Provides emscripten_webgpu_get_device and a few other helpers
#include <emscripten/html5_webgpu.h>

// They even have this header, reflecting the dawn cpp API!
#include <webgpu/webgpu_cpp.h>

int main() {

	printf("hello\n");

	auto device = emscripten_webgpu_get_device();

	return 0;
}

