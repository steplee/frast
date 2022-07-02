#pragma once

#include "frastVk/core/fvkApi.h"

// Simple 2d bitmap-text renderer.
// Would be easy to apply a model * viewProj,
// but for now, it is assumed that a flat output is wanted and just a model matrix is used.
//
// Font is generate in python code using freetype-py.
// It is converted to C code and stored in font.hpp,
// then read in the source file of this header.
class SimpleTextSet {
	public:
		SimpleTextSet(BaseApp* app);
		~SimpleTextSet();

		void render(RenderState& rs, Command &cmd);

		void reset();
		void setText(int i, const std::string& text, const float matrix[16], const float color[4]);
		void setAreaAndSize(float offx, float offy, float ww, float hh, float scale, const float mvp[16]);

	private:

		BaseApp* app = nullptr;

		static constexpr int maxLength = 176;
		static constexpr int maxStrings = 32;

		struct __attribute__((packed)) TextBufferHeader {
			// float offset[2];
			// float windowSize[2];
			float matrix[16];
			// float size[2];
		};
		struct __attribute__((packed)) TextBufferData {
			float matrix[16];
			float color[4];
			uint8_t chars[maxLength];
		};

		uint32_t stringLengths[maxStrings] = {0};
		ExBuffer ubo;
		ExImage fontTex;

		GraphicsPipeline pipeline;

		// vk::raii::DescriptorPool descPool = {nullptr};
		// vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
		// vk::raii::DescriptorSet descSet = {nullptr};
		DescriptorSet descSet;
		uint32_t ww,hh;

		Sampler sampler;
};
