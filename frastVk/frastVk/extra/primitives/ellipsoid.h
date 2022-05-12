#pragma once

#include "frastVk/core/app.h"
#include "frastVk/core/buffer_utils.h"


struct EllipsoidSet {

	struct EllipsoidData {
		float matrix[16];
		float color[4];
	};

		constexpr static int maxEllipsoids = 8;

		inline EllipsoidSet(BaseVkApp* app) : app(app) { }

		void renderInPass(RenderState& rs, vk::CommandBuffer cmd);
		void init(uint32_t subpass);

		void unset(int i);
		void set(int i, const float matrix[16], const float color[4]);

	private:

		bool residency[maxEllipsoids] = {false};
		int n_resident = 0;

		BaseVkApp* app;
		vk::raii::DescriptorPool descPool = {nullptr};
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};

		PipelineStuff pipelineStuff;

		// Stores inverse MVP, as well as each ellps model matrix
		ResidentBuffer globalBuffer;

};
