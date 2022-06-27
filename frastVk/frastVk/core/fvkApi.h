#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <utility>
#include <string>

#include <fmt/core.h>

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

};

struct Device {
	inline Device() : device(nullptr) {}
	inline Device(VkDevice& d, VkPhysicalDevice& pd, std::vector<uint32_t> ids) : device(d), pdevice(pd), queueFamilyGfxIdxs(ids) {}
	//Device(VkPhysicalDevice pdev);

	Device(const Device& d) = delete;
	Device& operator=(const Device& d) = delete;
	~Device();

	VkDevice device;
	VkPhysicalDevice pdevice;
	std::vector<uint32_t> queueFamilyGfxIdxs;

	inline operator VkDevice&() { return device; }
	inline operator const VkDevice&() const { return device; }
};

struct Queue {
	Queue(Device& dev, int family, int idx);
	~Queue();

	VkQueue queue;
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
	SwapChain(BaseApp& app);
	inline ~SwapChain() {
		if (displaySwapchain != nullptr)
			vkDestroySwapchainKHR(device, displaySwapchain, nullptr);
	}


	Device& device;
	VkSurfaceKHR surface = nullptr;
	VkSwapchainKHR displaySwapchain = nullptr;
	VkExtent2D size;
	VkSurfaceFormatKHR surfFormat;
	VkPresentModeKHR presentMode;
	uint32_t numImages=0;
	std::vector<VkImage> images;
	//std::vector<ExImage> depthImages;
	std::vector<VkImageView> imageViews;
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
	Device& device;
	VkCommandPool pool = nullptr;

	CommandPool(Device& d, int queueFamily);
	~CommandPool();

	std::vector<Command> allocate(int n);
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

	void submit(VkCommandBuffer* cmds, uint32_t n);
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
	ExImage swapchainImg { nullptr };

	Command cmd { nullptr };
	uint32_t scIndx=0, n=0;
	float time=0, dt=0;

	// bool useAcquireSema = true;
};

struct DescriptorSet {
};

// Unlike my previous code, where components had to allocate there own descriptor pools,
// assume the application will allocate enough up front so that they can be shared.
// This means that allocating/freeing descriptor sets could have problems in a multithread situation,
// but probably not an issue.
struct TheDescriptorPool {
	Device& device;
	VkDescriptorPool pool = nullptr;

	TheDescriptorPool(BaseApp& app);
	~TheDescriptorPool();
};


struct VertexInputDescription {
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	VkPipelineVertexInputStateCreateFlags flags;
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

struct SimpleRenderPass {
	VkDevice device { nullptr };
	VkRenderPass pass { nullptr };
	std::vector<VkFramebuffer> framebuffers;
	std::vector<ExImage> depthImages;
};

struct GraphicsPipeline {
	Device& device;
	VkPipeline pipeline { nullptr };
	std::vector<VkPushConstantRange> pushConstants;
	std::vector<VkDescriptorSetLayout> setLayouts;
	VkViewport viewport;
	VkRect2D scissor;
	VkPipelineLayout layout { nullptr };
	VkShaderModule vs{nullptr}, fs{nullptr};

	void create(float viewportXYWH[4], PipelineBuilder& builder, VkRenderPass pass, int subpass);

};








struct BaseApp {
	AppConfig cfg;

	VkInstance instance = nullptr;
	Device mainDevice;
	Queue mainQueue;
	Window* glfwWindow = nullptr;
	SwapChain swapchain;
	TheDescriptorPool dpool;

	CommandPool mainCommandPool;

	BaseApp(const AppConfig& cfg);
	~BaseApp();


	std::vector<FrameData> frameDatas;

	FrameData& acquireNextFrame();

	protected:

	SimpleRenderPass simpleRenderPass;
	bool make_basic_render_stuff();


	private:


	static constexpr uint32_t frameOverlap = 1;
	uint32_t frameIdx=0;

	void makeGlfwWindow();
	void makeRealSwapChain();

	void makeFrameDatas();
	void destroyFrameDatas();

	float timeZero = 0;
};

