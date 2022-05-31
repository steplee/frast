#pragma once

#include <fmt/color.h>

#include "app.h"

#define STATIC_SHADER 1

#if STATIC_SHADER
// namespace {
	// extern const std::vector<std::pair<std::string, std::string>> fvkCompiledShaders;
	// extern const int fvkNumShaders;
	#include "frastVk/shaders/compiled/all.cc"
// }
// #include "frastVk/shaders/compiled/all.hpp"
#else
#include <vector>
namespace {
std::vector<std::pair<std::string, std::string>> fvkCompiledShaders;
const int fvkNumShaders = 0;
}
#endif


int32_t find_idx_of_shader(const std::string& name) {
	// fmt::print(" - Searching {} shaders.\n", fvkNumShaders);
	for (int i=0; i<fvkNumShaders; i++) {
		std::string o = fvkCompiledShaders[i].first;
		// fmt::print(" - {} vs {}\n", name, o);
		if (fvkCompiledShaders[i].first == name) return i;
	}
	return -1;
}

bool loadOneShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs,
		const std::string& fullName) {
	if (STATIC_SHADER) {
		int32_t v_idx = find_idx_of_shader(fullName);
		if (v_idx < 0) {
			fmt::print(fmt::fg(fmt::color::red), " - [loadShader()] failed to find key '{}'\n", fullName);
			fflush(stdout);
			return true;
		}
		std::string v_spirv = fvkCompiledShaders[v_idx].second;
		vk::ShaderModuleCreateInfo vs_info { {}, v_spirv.length(), (uint32_t*) v_spirv.data() };
		vs = std::move(dev.createShaderModule(vs_info));
		return false;
	} else {
		assert(false);
	}
}


bool loadShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& baseNameV,
		const std::string& baseNameF) {
	
	if (STATIC_SHADER) {

		std::string v_key = baseNameV + ".v.glsl";
		std::string f_key = baseNameF + ".f.glsl";
		int32_t v_idx = find_idx_of_shader(v_key);
		int32_t f_idx = find_idx_of_shader(f_key);
		if (v_idx < 0) {
			fmt::print(fmt::fg(fmt::color::red), " - [loadShader()] failed to find key '{}'\n", v_key);
			fflush(stdout);
			return true;
		}
		if (f_idx < 0) {
			fmt::print(fmt::fg(fmt::color::red), " - [loadShader()] failed to find key '{}'\n", f_key);
			fflush(stdout);
			return true;
		}

		std::string v_spirv = fvkCompiledShaders[v_idx].second;
		std::string f_spirv = fvkCompiledShaders[f_idx].second;
		fmt::print(" - [loadShader()] creating '{}' '{}' from spirv (id {} {}) (len {} {})\n", baseNameV, baseNameF, v_idx,f_idx, v_spirv.length(),f_spirv.length());

		vk::ShaderModuleCreateInfo vs_info { {}, v_spirv.length(), (uint32_t*) v_spirv.data() };
		vk::ShaderModuleCreateInfo fs_info { {}, f_spirv.length(), (uint32_t*) f_spirv.data() };
		vs = std::move(dev.createShaderModule(vs_info));
		fs = std::move(dev.createShaderModule(fs_info));
		// return createShaderFromSpirv(dev, vs,fs, v_spirv.length(), f_spirv.length(), v_spirv.c_str(), f_spirv.c_str());
		return false;

	} else {
		const std::string vsp = "../frastVk/shaders/" + baseNameV + ".v.glsl";
		const std::string fsp = "../frastVk/shaders/" + baseNameF + ".f.glsl";
		return createShaderFromFiles(dev, vs,fs, vsp, fsp);
	}
}

// Give same base to V and F
bool loadShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& baseName) {
	return loadShader(dev, vs,fs, baseName, baseName);
}

bool loadShaderDynamic(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vPath,
		const std::string& fPath) {
	return createShaderFromFiles(dev, vs,fs, vPath, fPath);
}
