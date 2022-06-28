#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <utility>
#include <string>
#include <memory>

#include <fmt/core.h>

#include "render_state.h"
#include "window.h"
#include "fvkUtils.hpp"

class BaseApp;

class Device;
class AppConfig;
class SwapChain;
class FrameData;
class ExImage;
class ExBuffer;

/*
 *
 * The vk::raii C++ bindings were okay, and at least helpful to avoid some boilerplate when I was learning vulkan.
 * However, the compiles times they cause are excruciating.
 *
 * So here are my own C++ bindings, on the original Vulkan C API.
 * I cut a lot of corners that make using vk less general then the full API,
 * but it should be good for most circumstances.
 *
 * Some simplicities forced:
 *			- The BaseApp has one descirptor pool that should be shared by all components. Not thread-safe this way.
 *			- ExBuffer and ExImage ("ex" = exclusive, or external synch) must be externally synchronized
 *			- I DO NOT allow frame overlap. That is: rendering each frame is synchronous.
 *
 * TODO NOTE XXX:
 *		Is it a problem to not wait for the previous frame to finish rendering?
 *		Say a component updates an MVP matrix in a UBO being used by the previous frame, that is still rendering.
 *		Then that is a race condition, no?
 *		But the only way around this would be to have N copies of _all_ buffers (where N is swapchain count).
 *		But that is crazy inconvienent.
 *		So I guess the framework should have a facility for this N-way buffering...
 *
 *		TODO but now I just wait on the fence for each swapchain image render...
 *
 */

/////////////////////////////////////////////////////////////////
//     Implemented in fvkApi_detail.cc
/////////////////////////////////////////////////////////////////
Device makeGpuDeviceAndQueues(const AppConfig& cfg, VkInstance instance);

uint64_t scalarSizeOfFormat(const VkFormat& f);
uint32_t findMemoryTypeIndex(const VkPhysicalDevice& pdev, const VkMemoryPropertyFlags& flags, uint32_t maskOrZero=0);


struct AppConfig {

	struct DescriptorPoolConfig {
		int32_t maxSets = 64;
		std::vector<std::pair<int, uint32_t>> poolSizes = {
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 20 },
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20 },
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20 },
			// etc.
		};
	} descriptorPoolConfig;

	uint32_t width=512, height=512;
	bool depth = true;

	enum class WindowSys {
		eGlfw,
		eHeadless
	} windowSys = WindowSys::eGlfw;

	std::string title;

	inline std::vector<const char*> extraInstanceExtensions() const {
		if (windowSys == WindowSys::eGlfw) {
			return { VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_xcb_surface" };
		}
		return {};
	}
	inline std::vector<const char*> extraDeviceExtensions() const {
		if (windowSys == WindowSys::eGlfw) {
			return { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		}
		return {};
	}

	inline bool headless() const { return windowSys == WindowSys::eHeadless; }

};

struct Device {
	inline Device() : device(nullptr) {}
	inline Device(VkDevice& d, VkPhysicalDevice& pd, std::vector<uint32_t> ids) : device(d), pdevice(pd), queueFamilyGfxIdxs(ids) {}
	//Device(VkPhysicalDevice pdev);

	Device(const Device& d) = delete;
	Device& operator=(const Device& d) = delete;
	inline Device(Device&& d) { moveFrom(d); }
	inline Device& operator= (Device&& d) { moveFrom(d); return *this; }
	~Device();

	VkDevice device { nullptr };
	VkPhysicalDevice pdevice { nullptr };
	std::vector<uint32_t> queueFamilyGfxIdxs;

	inline operator VkDevice&() { return device; }
	inline operator const VkDevice&() const { return device; }
	inline void moveFrom(Device& o) {
		device = o.device;
		pdevice = o.pdevice;
		queueFamilyGfxIdxs = std::move(o.queueFamilyGfxIdxs);
		o.device = nullptr;
		o.pdevice = nullptr;
	}
};

struct Queue {
	inline Queue() : queue{nullptr}, family(0) {}
	Queue(Device& dev, uint32_t family, int idx);
	~Queue();

	VkQueue queue { nullptr };
	int family;
	inline operator VkQueue&() { return queue; }
	inline operator const VkQueue&() const { return queue; }
};

struct DeviceQueueSpec {
	Device& dev;
	Queue& q;
	inline VkPhysicalDevice& pdevice() { return dev.pdevice; }

	inline operator VkDevice&() const { return (VkDevice&)dev.device; }
	inline operator VkQueue&() const { return (VkQueue&)q.queue; }
};

// Either a real KHR swaphcain, or a fake one used with headless rendering
struct SwapChain {
	inline SwapChain() {};
	SwapChain(BaseApp& app);
	inline ~SwapChain() {
		if (displaySwapchain != nullptr)
			vkDestroySwapchainKHR(*device, displaySwapchain, nullptr);
	}


	Device* device { nullptr };
	VkSurfaceKHR surface = nullptr;
	VkSwapchainKHR displaySwapchain = nullptr;
	VkExtent2D size{0,0};
	VkSurfaceFormatKHR surfFormat;
	VkPresentModeKHR presentMode;
	uint32_t numImages=0;
	std::vector<ExImage> images;
	//std::vector<ExImage> depthImages;
	// std::vector<VkImageView> imageViews;

	inline SwapChain(const SwapChain& o) =delete;
	inline SwapChain& operator=(const SwapChain& o) =delete;
	inline SwapChain(SwapChain&& o) =default;
	inline SwapChain& operator=(SwapChain&& o) =default;
};

struct ExImage {
	VkDevice device { nullptr };
	VkImage img { nullptr };
	VkImageView view { nullptr };
	VkDeviceMemory mem { nullptr };
	VkImageLayout prevLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkExtent2D extent {0,0};
	VkFormat format { VK_FORMAT_UNDEFINED };
	uint32_t capacity = 0;

	// Memory details
	VkMemoryPropertyFlags memPropFlags;
	VkImageUsageFlags usageFlags;
	// ImageView details
	VkImageAspectFlags aspect;
	// Passed to sampler.
	bool unnormalizedCoordinates = false;

	inline uint32_t channels() const {
		return scalarSizeOfFormat(format);
	}
	inline uint64_t size() const {
		return scalarSizeOfFormat(format) * extent.width * extent.height;
	}

	void create(Device& device);

	// will not own!
	void setFromSwapchain(VkDevice device, VkImage& img, VkImageView& view, const VkExtent2D& ex, const VkImageLayout& layout, const VkImageAspectFlags& flags, const VkFormat& fmt);

	void* map();
	void unmap();
	void* mappedAddr = nullptr;
	bool own = true;

	inline ExImage() {};
	inline ExImage(const ExImage& o) =delete;
	inline ExImage& operator=(const ExImage& o) =delete;
	inline ExImage& operator=(ExImage&& o) { moveFrom(o); return *this; }
	inline ExImage(ExImage&& o) { moveFrom(o); }
	inline void moveFrom(ExImage& o) {
		img = o.img;
		own = o.own;
		device = o.device;
		view = o.view;
		mem = o.mem;
		prevLayout = o.prevLayout;
		extent = o.extent;
		format = o.format;
		capacity = o.capacity;
		memPropFlags = o.memPropFlags;
		usageFlags = o.usageFlags;
		aspect = o.aspect;
		unnormalizedCoordinates = o.unnormalizedCoordinates;
		mappedAddr = o.mappedAddr;
		o.img = nullptr;
		o.mappedAddr = nullptr;
	}
	~ExImage();
};

struct ExBuffer {
	VkDevice device { nullptr };
	VkBuffer buf { nullptr };
	VkDeviceMemory mem { nullptr };
	uint32_t capacity = 0;
	uint32_t givenSize = 0;
	VkMemoryPropertyFlags memPropFlags;
	VkBufferUsageFlags usageFlags;
	VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	void create(Device& device);

	void* map(VkDeviceSize offset=0, VkDeviceSize size=VK_WHOLE_SIZE);
	void unmap();
	void* mappedAddr = nullptr;
};

struct Barriers {
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	VkDependencyFlags depFlags = VK_DEPENDENCY_DEVICE_GROUP_BIT;

	std::vector<VkMemoryBarrier>       memBarriers;
	std::vector<VkBufferMemoryBarrier> bufBarriers;
	std::vector<VkImageMemoryBarrier>  imgBarriers;

	void append(ExImage& img, VkImageLayout to);
};

struct Command;
struct CommandPool {
	Device* device { nullptr };
	VkCommandPool pool { nullptr };

	inline CommandPool() {};
	CommandPool(Device& d, uint32_t queueFamily);
	CommandPool& operator=(const CommandPool& p) = delete;
	inline CommandPool& operator=(CommandPool&& p) { moveFrom(p); return *this; }
	inline CommandPool(CommandPool&& other) { moveFrom(other); }
	~CommandPool();

	std::vector<Command> allocate(int n);

	inline void moveFrom(CommandPool& o) {
		device = o.device;
		pool = o.pool;
		o.device = nullptr;
		o.pool = nullptr;
	}

};

struct Command {
	VkCommandBuffer cmdBuf;
	inline operator VkCommandBuffer& () { return cmdBuf; }

	void begin(VkCommandBufferUsageFlags flags={});
	void end();

	//void draw();
	void barriers(Barriers& b);
	void clearImage(ExImage& image, const std::vector<float>& color);

	void executeAndPresent(DeviceQueueSpec& qds, SwapChain& sc, FrameData& fd);

	// noop
	~Command();
};

// Temporary type used to submit a cmdbuf, with optional fences and semaphores
struct Submission {
	VkDevice& device;
	VkQueue& q;
	VkFence     fence { nullptr };
	std::vector<VkSemaphore> signalSemas;
	std::vector<VkSemaphore> waitSemas;
	std::vector<VkPipelineStageFlags> waitStages;
	uint64_t timeout = 999999999999lu;

	inline Submission(const DeviceQueueSpec& dqs) :
		device(dqs), q(dqs) {}

	void submit(VkCommandBuffer* cmds, uint32_t n, bool block=true);
};

struct FrameData {

	inline FrameData() {};
	FrameData(FrameData&& o) =default;
	FrameData(const FrameData& other) =delete;
	FrameData& operator=(const FrameData& other) =delete;

	VkFence frameDoneFence { nullptr };
	VkFence frameAvailableFence { nullptr };
	VkSemaphore swapchainAcquireSema { nullptr };
	VkSemaphore renderCompleteSema { nullptr };

	// The image in the swapchain. Set in BaseApp::acquireNext
	ExImage* swapchainImg { nullptr };
	//inline ExImage& swapchainImg(SwapChain& sc) { return sc.images[scIndx]; }

	Command cmd { nullptr };
	uint32_t scIndx=0, n=0;
	float time=0, dt=0;

	// bool useAcquireSema = true;
};

class TheDescriptorPool;
struct DescriptorSet {
	VkDescriptorSet dset { nullptr };
	VkDescriptorSetLayout layout { nullptr };
	TheDescriptorPool* pool { nullptr };

    uint32_t              descriptorCount = 0;
    VkShaderStageFlags    stageFlags;

	void create(TheDescriptorPool& pool);

	inline operator VkDescriptorSet&() { return dset; }
};

// Unlike my previous code, where components had to allocate there own descriptor pools,
// assume the application will allocate enough up front so that they can be shared.
// This means that allocating/freeing descriptor sets could have problems in a multithread situation,
// but probably not an issue.
struct TheDescriptorPool {
	Device* device { nullptr };
	VkDescriptorPool pool = nullptr;

	inline TheDescriptorPool() {}
	TheDescriptorPool(BaseApp& app);
	~TheDescriptorPool();

	inline operator VkDescriptorPool&() { return pool; }
};


struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	VkPipelineVertexInputStateCreateFlags flags=0;
};

// Holds temporary data needed to build a GraphicsPipeline, but not needed afterward
struct PipelineBuilder {
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineRasterizationStateCreateInfo rasterizer;
	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo multisampling;
	VkPipelineDepthStencilStateCreateInfo depthState;

	bool additiveBlending = false;
	bool replaceBlending = false;
	bool depthTest = true, depthWrite = true;

    VkPipelineCreateFlags                            flags;

	virtual void init(
			const VertexInputDescription& vertexDesc,
			VkPrimitiveTopology topo,
			VkShaderModule vs, VkShaderModule fs);
};

// TODO: This only supports having exactly one depth and exactly one color attachment right now...
struct RenderPassDescription {
	std::vector<VkAttachmentDescription> attDescriptions;
	// std::vector<VkAttachmentReference> attRefs;
	// VkAttachmentDescription colorDesc, depthDesc;
	VkAttachmentReference colorRef, depthRef;
	std::vector<VkSubpassDescription> subpassDescriptions;
	std::vector<VkSubpassDependency> subpassDeps;

	//VkRenderPassCreateInfo rpInfo;

	inline RenderPassDescription() {};

	// No move operators/ctor
	RenderPassDescription& operator=(RenderPassDescription&& o) = delete;
	RenderPassDescription(RenderPassDescription&& o) = delete;

	// Copy op/ctor must replace pointers in Vk structs!
	inline RenderPassDescription& operator=(const RenderPassDescription& o) { copyFrom(o); return *this; }
	RenderPassDescription(const RenderPassDescription& o) { copyFrom(o); }
	inline void copyFrom(const RenderPassDescription& o) {
		attDescriptions = o.attDescriptions;
		colorRef = o.colorRef;
		depthRef = o.depthRef;
		subpassDescriptions = o.subpassDescriptions;
		subpassDeps = o.subpassDeps;
		for (int i=0; i<subpassDescriptions.size(); i++) {
			auto& sp = subpassDescriptions[i];
			// for (int j=0; j<sp.inputAttachmentCount; j++) sd.pInputAttachments = &inputAttachments[i];
			// for (int j=0; j<sp.colorAttachmentCount; j++) sd.pColorAttachments = &attRefs[i];
			assert(sp.inputAttachmentCount == 0);
			assert(sp.colorAttachmentCount == 0 or sp.colorAttachmentCount == 1);
			if (sp.colorAttachmentCount == 1) sp.pColorAttachments = &colorRef;
			if (sp.pDepthStencilAttachment) sp.pDepthStencilAttachment = &depthRef;
		}
	}

	VkRenderPassCreateInfo makeCreateInfo();
};

struct SimpleRenderPass {
	VkDevice device { nullptr };
	VkRenderPass pass { nullptr };
	std::vector<VkFramebuffer> framebuffers;
	std::vector<ExImage> depthImages;
	VkImageLayout inputLayout, outputLayout;

	inline operator VkRenderPass&() { return pass; }

	void begin(Command& cmd, FrameData& fd);
	void end(Command& cmd, FrameData& fd);

	uint32_t framebufferWidth, framebufferHeight;

	RenderPassDescription description;
};

struct GraphicsPipeline {
	Device* device { nullptr };
	VkPipeline pipeline { nullptr };
	std::vector<VkPushConstantRange> pushConstants;
	std::vector<VkDescriptorSetLayout> setLayouts;
	VkViewport viewport;
	VkRect2D scissor;
	VkPipelineLayout layout { nullptr };
	VkShaderModule vs{nullptr}, fs{nullptr};

	void create(Device& d, float viewportXYWH[4], PipelineBuilder& builder, VkRenderPass pass, int subpass);

	~GraphicsPipeline();

	inline operator VkPipeline&() { return pipeline; }
};








struct BaseApp : public UsesIO {
	public:

	AppConfig cfg;

	VkInstance instance = nullptr;
	Device mainDevice;
	Queue mainQueue;
	Window* glfwWindow = nullptr;
	SwapChain swapchain;
	TheDescriptorPool dpool;

	CommandPool mainCommandPool;

	BaseApp(const AppConfig& cfg);
	virtual ~BaseApp();


	std::vector<FrameData> frameDatas;

	FrameData& acquireNextFrame();

	virtual void render();
	virtual void doRender(RenderState& rs) =0;
	inline virtual void postRender() {}

		// inline virtual bool handleKey(int key, int scancode, int action, int mods) override { return false; }
		// inline virtual bool handleMousePress(int button, int action, int mods) override { return false; }
		// inline virtual bool handleMouseMotion(double x, double y) override { return false; }

	virtual void initVk();

	protected:

	// RenderPassDescription simpleRenderPassDescription;
	SimpleRenderPass simpleRenderPass;
	bool make_basic_render_stuff();

	std::shared_ptr<Camera> camera = nullptr;

	inline bool isDone() const { return isDone_; }

	uint32_t windowWidth, windowHeight;

	private:


	static constexpr uint32_t frameOverlap = 1;
	uint32_t frameIdx=0;

	void makeGlfwWindow();
	void makeRealSwapChain();

	void makeFrameDatas();
	void destroyFrameDatas();

	float timeZero = 0;

	private:
		int renders = 0;
		float time0 = 0;
		float lastTime = 0;
	protected:
		float fpsMeter = 0;
		RenderState renderState;
		bool isDone_ = false;
};

