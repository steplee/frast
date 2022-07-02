#pragma once

#include "frastVk/core/fvkApi.h"


struct EllipsoidSet {

	struct EllipsoidData {
		float matrix[16];
		float color[4];
	};

		constexpr static int maxEllipsoids = 8;

		inline EllipsoidSet(BaseApp* app) : app(app) { }

		void render(RenderState& rs, Command& cmd);
		void init();

		void unset(int i);
		void set(int i, const float matrix[16], const float color[4]);

	private:

		bool residency[maxEllipsoids] = {false};
		int n_resident = 0;

		BaseApp* app;
		// vk::raii::DescriptorPool descPool = {nullptr};
		// vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		// vk::raii::DescriptorSet globalDescSet = {nullptr};

		// PipelineStuff pipelineStuff;

		DescriptorSet descSet;
		GraphicsPipeline pipeline;

		// Stores inverse MVP, as well as each ellps model matrix
		ExBuffer globalBuffer;

};
