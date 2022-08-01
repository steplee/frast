#pragma once


#include "frastVk/core/fvkApi.h"

//
//
// In the old implementation, this supported a up-down filtered version
// that made the particles agglomerate into a cloud.
//
// But I don't fell like implementing all the FBOs and passes in order to do that.
//
// So right now, only ePoints and eNone are supported.
// ePoints uses particlePipeline but outputPass, which is confusing, and
// only uses the external FBO (no fbos created by this class, no blit/quad needed to blend)
//
//

class RenderState;

class ParticleRenderPass {
};
struct __attribute__((aligned)) ParticleCloudPushConstants {
	float w, h;
	float s;
	float d;
};

enum class ParticleRenderMode {
	eFiltered, ePoints, eNone
};

class ParticleCloudRenderer {

	public:

		ParticleCloudRenderer(BaseApp* app, int capacity);


		// 1) Splat particles onto internal framebuffer, with depth testing from scene depthmap
		// 2) Blur pyramidally, using ping-pong textures, down then back up to full res
		// 3) Blend this new texture with the screen fbo (this can be done by rendering a full-screen quad)
		void render(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer);

		// Should only be called in render thread!
		void uploadParticles(std::vector<float>& particles, bool normalizeMaxInplace=true, float divisor=1.f);

		inline void setRenderMode(ParticleRenderMode mode_) { mode = mode_; }

	private:

		void renderFiltered(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer);
		void renderPoints(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer);
		ParticleRenderMode mode = ParticleRenderMode::eFiltered;

		uint32_t w, h;
		uint32_t n_lvl = 3;
		// uint32_t n_lvl = 5;

		BaseApp* app = nullptr;

		void setup(int capacity);
		void setup_buffers(int capacity);
		void setup_particle_buffer(int capacity_);
		void setup_fbos();
		void setup_pipelines();

		GraphicsPipeline particlePipeline;
		GraphicsPipeline upPipeline;
		GraphicsPipeline downPipeline;
		GraphicsPipeline outputPipeline;
		// std::vector<PipelineStuff> upPipelineStuffs,
								   // downPipelineStuffs;

		SimpleRenderPass particlePass, outputPass;
		// vk::raii::RenderPass filterPass { nullptr };
		// vk::raii::RenderPass outputPass { nullptr };

		ExBuffer particleBuffer;
		uint32_t numParticles = 0;

		// std::vector<ResidentImage> particleImages;
		// std::vector<ResidentImage> filterImages[2];
		std::vector<ExImage> particleImages;
		ExImage filterImages[2];
		// std::vector<std::vector<vk::raii::ImageView>> filterImageViews[2];

		// std::vector<vk::raii::Framebuffer> filterFramebuffers[2];
		// std::vector<vk::raii::Framebuffer> particleFramebuffers;
		// vk::raii::Framebuffer filterFramebuffers[2] = {{nullptr},{nullptr}};
		std::vector<ExImage> texs;

		DescriptorSet globalDescSet, particleDescSet;
		DescriptorSet filterDescSet[2];
		ExBuffer globalBuffer;
		int capacity = 0;

};

