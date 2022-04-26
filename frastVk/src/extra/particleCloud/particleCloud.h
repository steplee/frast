#pragma once


#include "core/buffer_utils.h"
#include "core/app.h"

#include <vulkan/vulkan_raii.hpp>

class RenderState;

class ParticleRenderPass {
};
struct __attribute__((aligned)) ParticleCloudPushConstants {
	uint32_t w, h;
	float s;
};

class ParticleCloudRenderer {

	public:

		ParticleCloudRenderer(BaseVkApp* app);


		// 1) Splat particles onto internal framebuffer, with depth testing from scene depthmap
		// 2) Blur pyramidally, using ping-pong textures, down then back up to full res
		// 3) Blend this new texture with the screen fbo (this can be done by rendering a full-screen quad)
		vk::CommandBuffer render(RenderState& rs, vk::Framebuffer outputFramebuffer);

		// Should only be called in render thread!
		void uploadParticles(const std::vector<float>& particles);

	private:

		uint32_t w, h;
		uint32_t n_lvl = 3;

		BaseVkApp* app = nullptr;

		void setup();
		void setup_buffers();
		void setup_fbos();
		void setup_pipelines();

		PipelineStuff particlePipelineStuff;
		PipelineStuff upPipelineStuff;
		PipelineStuff downPipelineStuff;
		PipelineStuff outputPipelineStuff;
		// std::vector<PipelineStuff> upPipelineStuffs,
								   // downPipelineStuffs;

		vk::raii::RenderPass particlePass { nullptr };
		vk::raii::RenderPass filterPass { nullptr };
		vk::raii::RenderPass outputPass { nullptr };

		ResidentBuffer particleBuffer;
		uint32_t numParticles = 0;

		// std::vector<ResidentImage> particleImages;
		// std::vector<ResidentImage> filterImages[2];
		std::vector<ResidentImage> particleImages;
		ResidentImage filterImages[2];
		// std::vector<std::vector<vk::raii::ImageView>> filterImageViews[2];

		std::vector<vk::raii::Framebuffer> particleFramebuffers;
		// std::vector<vk::raii::Framebuffer> filterFramebuffers[2];
		vk::raii::Framebuffer filterFramebuffers[2] = {{nullptr},{nullptr}};
		std::vector<ResidentImage> texs;

		std::vector<vk::raii::CommandBuffer> cmdBuffers;

		vk::raii::DescriptorPool descPool = {nullptr};
		ResidentBuffer globalBuffer;
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera
		vk::raii::DescriptorSetLayout filterDescSetLayout = {nullptr}; // holds texture bindings
		vk::raii::DescriptorSet globalDescSet = {nullptr};
		std::vector<vk::raii::DescriptorSet> particleDescSet;
		vk::raii::DescriptorSet filterDescSet[2] = {{nullptr},{nullptr}};

};

