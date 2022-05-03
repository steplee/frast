#pragma once

#include "frastVk/core/app.h"
#include "frastVk/core/buffer_utils.h"

// Simple 2d bitmap-text renderer.
// Would be easy to apply a model * viewProj,
// but for now, it is assumed that a flat output is wanted and just a model matrix is used.
//
// Font is generate in python code using freetype-py.
// It is converted to C code and stored in font.hpp,
// then read in the source file of this header.
class SimpleTextSet {
	public:
		SimpleTextSet(BaseVkApp* app);

		void render(RenderState& rs, vk::CommandBuffer &cmd);

		void reset();
		void setText(int i, const std::string& text, const float matrix[16]);

	private:

		BaseVkApp* app = nullptr;

		static constexpr int maxLength = 132;
		static constexpr int maxStrings = 32;

		struct __attribute__((packed)) TextBufferData {
			float matrix[16];
			uint8_t chars[maxLength];
		};
		ResidentBuffer ubo;
		uint32_t stringLengths[maxStrings] = {0};

		ResidentImage fontTex;

		PipelineStuff pipelineStuff;

		vk::raii::DescriptorPool descPool = {nullptr};
		vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
		vk::raii::DescriptorSet descSet = {nullptr};
		uint32_t ww,hh;
};
