#pragma once


// This is useful for easier experimentation in the frastVk source tree.
// For experimentation out-of source tree, use loadShaderDynamic.
//
// To support out-of source precompiled SPIRV, I should move all this code
// to the generated .hpp file, and put everything behind a namespace unique to that hpp.
//
#define STATIC_SHADER 1
// #define STATIC_SHADER 0

#if STATIC_SHADER
// namespace {
	// extern const std::vector<std::pair<std::string, std::string>> fvkCompiledShaders;
	// extern const int fvkNumShaders;
// }
// #include "frastVk/shaders/compiled/all.hpp"
#else
#include <vector>
namespace {
std::vector<std::pair<std::string, std::string>> fvkCompiledShaders;
const int fvkNumShaders = 0;
}
#endif

int32_t find_idx_of_shader(const std::string& name);

bool loadOneShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs,
		const std::string& baseName);
		

bool loadShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& baseNameV,
		const std::string& baseNameF);

// Give same base to V and F
bool loadShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& baseName);

bool loadShaderDynamic(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vPath,
		const std::string& fPath);
