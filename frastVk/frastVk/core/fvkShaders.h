#pragma once

#include <string>
#include <vulkan/vulkan.h>


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
