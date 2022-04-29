#pragma once

#include <fmt/color.h>
#include "frastVk/shaders/compiled/all.hpp"

// This is useful for easier experimentation in the frastVk source tree.
// For experimentation out-of source tree, use loadShaderDynamic.
//
// To support out-of source precompiled SPIRV, I should move all this code
// to the generated .hpp file, and put everything behind a namespace unique to that hpp.
//
#define STATIC_SHADER 1
// #define STATIC_SHADER 0


inline int32_t find_idx_of_shader(const std::string& name) {
	for (int i=0; i<_NumShaders; i++) {
		if (_compiledShaders[i].first == name) return i;
	}
	return -1;
}


inline bool loadShader(
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

		std::string v_spirv = _compiledShaders[v_idx].second;
		std::string f_spirv = _compiledShaders[f_idx].second;
		fmt::print(" - [loadShader()] creating '{}' '{}' from spirv (id {} {}) (len {} {})\n", baseNameV, baseNameF, v_idx,f_idx, v_spirv.length(),f_spirv.length());

		vk::ShaderModuleCreateInfo vs_info { {}, v_spirv.length(), (uint32_t*) v_spirv.data() };
		vk::ShaderModuleCreateInfo fs_info { {}, f_spirv.length(), (uint32_t*) f_spirv.data() };
		vs = std::move(dev.createShaderModule(vs_info));
		fs = std::move(dev.createShaderModule(fs_info));
		// return createShaderFromSpirv(dev, vs,fs, v_spirv.length(), f_spirv.length(), v_spirv.c_str(), f_spirv.c_str());

	} else {
		const std::string vsp = "../frastVk/shaders/" + baseNameV + ".v.glsl";
		const std::string fsp = "../frastVk/shaders/" + baseNameF + ".f.glsl";
		return createShaderFromFiles(dev, vs,fs, vsp, fsp);
	}
}

// Give same base to V and F
inline bool loadShader(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& baseName) {
	return loadShader(dev, vs,fs, baseName, baseName);
}

inline bool loadShaderDynamic(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vPath,
		const std::string& fPath) {
	return createShaderFromFiles(dev, vs,fs, vPath, fPath);
}
