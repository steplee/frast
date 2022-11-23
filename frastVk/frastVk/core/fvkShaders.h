#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <array>
#include <fmt/core.h>
#include <fmt/color.h>

#include "fvkApi.h"

template <int N>
int find_idx_of_shader_(std::array<std::pair<std::string,std::string>, N>& shaders, const std::string& name) {
    for (int i=0; i<N; i++) if (shaders[i].first == name) return i;
    return -1;
}

// New API
// Python script will generate both header and cc file.
// Component should include header, but not the cc anymore.
// The cc will implement these functions and store data in a global variable.
template <int N>
struct FvkShaderGroup {
	// std::unordered_map<std::string, std::string> shaders;
	std::array<std::pair<std::string, std::string>, N> shaders;

	// bool loadShader(
			// VkDevice& dev,
			// VkShaderModule& vs, VkShaderModule& fs,
			// const std::string& baseNameV,
			// const std::string& baseNameF);

		inline
		bool loadShader(VkDevice& dev,
				VkShaderModule& vs, VkShaderModule& fs,
				const std::string& baseNameV,
				const std::string& baseNameF) {

			std::string v_key = baseNameV + ".v.glsl";
			std::string f_key = baseNameF + ".f.glsl";
			int32_t v_idx = find_idx_of_shader_<N>(shaders, v_key);
			int32_t f_idx = find_idx_of_shader_<N>(shaders, f_key);
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

			std::string v_spirv = shaders[v_idx].second;
			std::string f_spirv = shaders[f_idx].second;
			fmt::print(" - [loadShader()] creating '{}' '{}' from spirv (id {} {}) (len {} {})\n", baseNameV, baseNameF, v_idx,f_idx, v_spirv.length(),f_spirv.length());

			VkShaderModuleCreateInfo vs_info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, {}, v_spirv.length(), (uint32_t*) v_spirv.data() };
			VkShaderModuleCreateInfo fs_info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, {}, f_spirv.length(), (uint32_t*) f_spirv.data() };
			assertCallVk(vkCreateShaderModule(dev, &vs_info, nullptr, &vs));
			assertCallVk(vkCreateShaderModule(dev, &fs_info, nullptr, &fs));
			return false;
		}


	// inf find_idx(const std::string& name);
};


bool loadShader(
		VkDevice& dev,
		VkShaderModule& vs, VkShaderModule& fs,
		const std::string& baseNameV,
		const std::string& baseNameF);

// Give same base to V and F
bool loadShader(
		VkDevice& dev,
		VkShaderModule& vs, VkShaderModule& fs,
		const std::string& baseName);

bool loadShaderDynamic(
		VkDevice& dev,
		VkShaderModule& vs, VkShaderModule& fs,
		const std::string& vPath,
		const std::string& fPath);
