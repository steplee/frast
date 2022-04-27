#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <vector>


#include "buffer_utils.h"
#include "window.hpp"
#include "render_state.h"

struct ResidentMesh;

uint32_t findMemoryTypeIndex(const vk::PhysicalDevice& pdev, const vk::MemoryPropertyFlags& flags);

struct SimpleRenderPass {
	vk::raii::RenderPass pass { nullptr };
	std::vector<vk::raii::Framebuffer> framebuffers;

	std::vector<ResidentImage> depthImages;
	//ResidentImage depthImage;
};

// Temporary data used for pipeline creation. Contains no 'real' data.
struct PipelineBuilder {
	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	vk::PipelineRasterizationStateCreateInfo rasterizer;
	vk::PipelineColorBlendAttachmentState colorBlendAttachment;
	vk::PipelineMultisampleStateCreateInfo multisampling;
	vk::PipelineDepthStencilStateCreateInfo depthState;

	bool additiveBlending = false;
	bool depthTest = true, depthWrite = true;

	virtual void init(
			const VertexInputDescription& vertexDesc,
			vk::PrimitiveTopology topo,
			vk::ShaderModule vs, vk::ShaderModule fs);
};

// Long-lived pipeline data.
struct PipelineStuff {
	vk::Viewport viewport;
	vk::Rect2D scissor;
	vk::raii::PipelineLayout pipelineLayout { nullptr };
	vk::raii::Pipeline pipeline { nullptr };

	vk::raii::ShaderModule vs{nullptr}, fs{nullptr};

	std::vector<vk::PushConstantRange> pushConstants;
	std::vector<vk::DescriptorSetLayout> setLayouts;

	bool setup_viewport(float w, float h, float x=0, float y=0);
	bool build(PipelineBuilder& builder, vk::raii::Device& device, const vk::RenderPass& pass, uint32_t subpass);
};

bool createShaderFromStrings(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vsrc, const std::string& fsrc);
bool createShaderFromFiles(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vsrcPath, const std::string& fsrcPath);
bool createShaderFromSpirv(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		size_t v_spirv_len, size_t f_spirv_len,
		const char* v_spirv, const char* f_spirv);




struct FrameData {
	vk::raii::Semaphore scAcquireSema { nullptr };
	vk::raii::Semaphore renderCompleteSema { nullptr };
	vk::raii::Fence frameDoneFence { nullptr };
	vk::raii::Fence frameReadyFence { nullptr };

	vk::raii::CommandBuffer cmd { nullptr };
	uint32_t scIndx=0;
	float time=0, dt=0;
	int n = 0;
};

// A wrapper around vk::raii::SwapchainKHR or a simple custom implementation
// for the headless case.
struct AbstractSwapchain {

	vk::raii::SwapchainKHR sc { nullptr };

	std::vector<vk::raii::Image> headlessImages;
	std::vector<vk::raii::DeviceMemory> headlessMemory;
	std::vector<vk::raii::CommandBuffer> headlessCopyCmds;
	std::vector<vk::raii::Semaphore> headlessCopyDoneSemas;
	std::vector<vk::raii::Fence> headlessCopyDoneFences;


	inline std::vector<vk::Image> getImages() {
		if (!headlessImages.size()) {
			std::vector<vk::Image> x;
			for (auto &i : sc.getImages()) x.push_back(vk::Image{i});
			return x;
		}
		else {
			std::vector<vk::Image> x;
			for (auto &i : headlessImages) x.push_back(*i);
			return x;
		}
	}
	inline vk::Image getImage(int i) {
		if (!headlessImages.size()) return vk::Image{sc.getImages()[i]};
		else return *headlessImages[i];
	}

	uint32_t acquireNextImage(vk::raii::Device& device, vk::Semaphore sema, vk::Fence fence);
	private:
	int curIdx = 0;
};

/*
 *
 * A base class for a vulkan graphics app.
 *
 */
struct BaseVkApp : public Window {

	BaseVkApp();
	virtual void init();

	bool headless = false;

	vk::raii::Context ctx;
	vk::raii::Instance instance { nullptr };

	vk::raii::Device deviceGpu { nullptr };
	vk::raii::Device deviceIntegratedGpu { nullptr };

	vk::raii::PhysicalDevice pdeviceGpu { nullptr };
	vk::raii::PhysicalDevice pdeviceIntegratedGpu { nullptr };

	vk::raii::Queue queuePresent { nullptr };
	vk::raii::Queue queueGfx { nullptr };
	vk::raii::Queue queueCompute { nullptr };
	std::vector<uint32_t> queueFamilyGfxIdxs;

	vk::raii::SurfaceKHR surface { nullptr };
	// vk::raii::SwapchainKHR sc { nullptr };
	AbstractSwapchain sc;

	vk::SurfaceFormatKHR scSurfaceFormat;
	std::vector<vk::raii::ImageView> scImageViews;
	int scNumImages = 0;
	int frameOverlap = 0;
	inline vk::Image getSwapChainImage(int i) {
		return sc.getImage(i);
		// return vk::Image(sc.getImages()[i]);
	}
	inline const vk::ImageView& getSwapChainImageView(int i) {
		return *scImageViews[i];
	}

	vk::raii::CommandPool commandPool { nullptr };
	//std::vector<vk::raii::CommandBuffer> commandBuffers;

	SimpleRenderPass simpleRenderPass;

	Uploader uploader;

	inline virtual uint32_t mainSubpass() const { return 0; }

	protected:
		bool require_16bit_shader_types = true;
		bool make_instance();
		bool make_gpu_device();

		bool make_surface();
		bool make_swapchain();
		bool make_headless_swapchain();

		bool make_frames();
		bool make_basic_render_stuff();

		FrameData& acquireFrame();

		float time();
		std::vector<FrameData> frameDatas;
	private:
		int renders = 0;
		float time0 = 0;
		float lastTime = 0;
		float fpsMeter = 0;

};


class VkApp : public BaseVkApp {
	public:

		VkApp();
		~VkApp();
		virtual void init() override;

		// VkApp provides standard starter function that further calls doRender()
		virtual void render();
		virtual void doRender(RenderState& rs) =0;
		inline virtual void handleCompletedHeadlessRender(RenderState& rs) {};

		bool isDone();

		virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) override;

		ResidentMesh simpleMesh;

		vk::raii::DescriptorPool descPool { nullptr };
		vk::raii::DescriptorSetLayout globalDescLayout { nullptr };
		vk::raii::DescriptorSet globalDescSet { nullptr };

	public:
		bool isDone_ = false;

		PipelineStuff simplePipelineStuff;
		bool createSimplePipeline(PipelineStuff& out, vk::RenderPass pass);

		PipelineStuff texturedPipelineStuff;
		bool createTexturedPipeline(PipelineStuff& out, ResidentMesh& mesh, vk::RenderPass pass);


		void initDescriptors();

		ResidentImage myTex;
		vk::raii::DescriptorSetLayout texDescLayout { nullptr };
		vk::raii::DescriptorSet texDescSet { nullptr };

		ResidentBuffer camBuffer;
		std::shared_ptr<Camera> camera = nullptr;
		RenderState renderState;


};

