#include "fvkShaders.h"
#include "fvkApi.h"

#define STATIC_SHADER 1

#include <cassert>
#include <fmt/core.h>
#include <fmt/color.h>
#include "frastVk/shaders/compiled/all.cc"


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
		VkDevice& dev,
		VkShaderModule& vs,
		const std::string& fullName) {
#ifdef STATIC_SHADER
		int32_t v_idx = find_idx_of_shader(fullName);
		if (v_idx < 0) {
			fmt::print(fmt::fg(fmt::color::red), " - [loadShader()] failed to find key '{}'\n", fullName);
			fflush(stdout);
			return true;
		}
		std::string v_spirv = fvkCompiledShaders[v_idx].second;
		VkShaderModuleCreateInfo vs_info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, {}, v_spirv.length(), (uint32_t*) v_spirv.data() };
		assertCallVk(vkCreateShaderModule(dev, &vs_info, nullptr, &vs));
		return false;
#else
		assert(false);
#endif
}


bool loadShader(
		VkDevice& dev,
		VkShaderModule& vs, VkShaderModule& fs,
		const std::string& baseNameV,
		const std::string& baseNameF) {
	
#ifdef STATIC_SHADER

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

		VkShaderModuleCreateInfo vs_info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, {}, v_spirv.length(), (uint32_t*) v_spirv.data() };
		VkShaderModuleCreateInfo fs_info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, {}, f_spirv.length(), (uint32_t*) f_spirv.data() };
		assertCallVk(vkCreateShaderModule(dev, &vs_info, nullptr, &vs));
		assertCallVk(vkCreateShaderModule(dev, &fs_info, nullptr, &fs));
		return false;

#else

		const std::string vsp = "../frastVk/shaders/" + baseNameV + ".v.glsl";
		const std::string fsp = "../frastVk/shaders/" + baseNameF + ".f.glsl";
		return createShaderFromFiles(dev, vs,fs, vsp, fsp);

#endif

}

// Give same base to V and F
bool loadShader(
		VkDevice& dev,
		VkShaderModule& vs, VkShaderModule& fs,
		const std::string& baseName) {
	return loadShader(dev, vs,fs, baseName, baseName);
}

bool loadShaderDynamic(
		VkDevice& dev,
		VkShaderModule& vs, VkShaderModule& fs,
		const std::string& vPath,
		const std::string& fPath) {
	assert(false);
	//return createShaderFromFiles(dev, vs,fs, vPath, fPath);
}
