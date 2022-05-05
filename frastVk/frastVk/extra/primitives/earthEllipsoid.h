#pragma once

#include "frastVk/core/app.h"
#include "frastVk/core/buffer_utils.h"


struct EarthEllipsoid {

	inline EarthEllipsoid(BaseVkApp* app) : app(app) { }

		void renderInPass(RenderState& rs, vk::CommandBuffer cmd);
		void init(uint32_t subpass);

	private:

		BaseVkApp* app;
		vk::raii::DescriptorPool descPool = {nullptr};
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};

		// Stores inverse mvp with focal lengths
		ResidentBuffer globalBuffer;
		PipelineStuff pipelineStuff;
};

