#include "fvkApi.h"
#include <iostream>
#include <fmt/color.h>

#include <chrono>
namespace {
static std::chrono::time_point<std::chrono::high_resolution_clock> __tp0;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback_(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    // std::cout << "validation layer: " << pCallbackData->pMessage << std::endl;
	fmt::print(fmt::fg(fmt::color::orange), "{}\n", pCallbackData->pMessage);

    return VK_FALSE;
}

}
float getSeconds(bool first) {
	if (first) {
		__tp0 = std::chrono::high_resolution_clock::now();
	}
	auto tp = std::chrono::high_resolution_clock::now();
	return static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(tp - __tp0).count()) * .000001f;
}

/////////////////////////////////////////////////////////////////////
//         Implementation
/////////////////////////////////////////////////////////////////////

/*
Device::Device(VkPhysicalDevice pdev) {

	std::vector<VkDeviceQueueCreateInfo> qCreateInfos;
	std::vector<const char*> layers;
	std::vector<const char*> exts;
	VkPhysicalDeviceFeatures features;

	VkDeviceCreateInfo ci;
	ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	ci.pNext = nullptr;
	ci.flags = {};
	ci.queueCreateInfoCount = qCreateInfos.size();
	ci.pQueueCreateInfos = qCreateInfos.data();
	ci.enabledLayerCount = layers.size();
	ci.ppEnabledLayerNames = layers.data();
	ci.enabledExtensionCount = exts.size();
	ci.ppEnabledExtensionNames = exts.data();
	ci.pEnabledFeatures = &features;
	vkCreateDevice(pdev, &ci, nullptr, &device);
}
*/
Device::~Device() {
	if (device != nullptr) {
		fmt::print(" - Destroying device!\n");
		vkDestroyDevice(device, nullptr);
	}
}

Queue::Queue(Device& d, uint32_t family_, int idx) : family(family_) {
	vkGetDeviceQueue(d.device, family, idx, &queue);
}
Queue::~Queue() {
	queue = nullptr;
}

CommandPool::CommandPool(Device& d, uint32_t queueFamily) : device(&d) {
	VkCommandPoolCreateInfo ci;
	ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	ci.pNext = nullptr;
	ci.queueFamilyIndex = queueFamily;
	ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	assertCallVk(vkCreateCommandPool(d.device, &ci, nullptr, &pool));
}
CommandPool::~CommandPool() {
	if (pool != nullptr)
		vkDestroyCommandPool(device->device, pool, nullptr);
}
std::vector<Command> CommandPool::allocate(int n) {
	std::vector<VkCommandBuffer> buffers_(n);
	std::vector<Command> out(n);
	VkCommandBufferAllocateInfo ai;
	ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ai.pNext = nullptr;
	ai.commandPool = this->pool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = n;
	assertCallVk(vkAllocateCommandBuffers(device->device, &ai, buffers_.data()));
	for (int i=0; i<n; i++) out[i].cmdBuf = buffers_[i];
	return out;
}

Command::~Command() {
}

Command& Command::begin(VkCommandBufferUsageFlags flags) {

	assertCallVk(vkResetCommandBuffer(cmdBuf, {}));

	VkCommandBufferBeginInfo bi;
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.pNext = nullptr;
	bi.flags = flags;
	bi.pInheritanceInfo = nullptr;
	assertCallVk(vkBeginCommandBuffer(cmdBuf, &bi));
	return *this;
}
Command& Command::end() {
	assertCallVk(vkEndCommandBuffer(cmdBuf));
	return *this;
}

Command& Command::barriers(Barriers& b) {
	if (b.memBarriers.size() or b.bufBarriers.size() or b.imgBarriers.size())
		vkCmdPipelineBarrier(cmdBuf, b.srcStageMask, b.dstStageMask, b.depFlags,
				(uint32_t)b.memBarriers.size(), b.memBarriers.data(),
				(uint32_t)b.bufBarriers.size(), b.bufBarriers.data(),
				(uint32_t)b.imgBarriers.size(), b.imgBarriers.data());
	return *this;
}

Command& Command::clearImage(ExImage& image, const std::vector<float>& color) {

	// Might have to transition
	if (image.prevLayout != VK_IMAGE_LAYOUT_GENERAL and image.prevLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		Barriers barriers_;
		barriers_.append(image, VK_IMAGE_LAYOUT_GENERAL);
		this->barriers(barriers_);
	}

	VkImageSubresourceRange range { image.aspect, 0, 1, 0, 1};
	vkCmdClearColorImage(cmdBuf, image.img, image.prevLayout, (VkClearColorValue*)color.data(), 1, &range);

	return *this;
}

Command& Command::copyImageToBuffer(ExBuffer& out, ExImage& in, VkImageLayout finalLayout_) {
	VkImageLayout finalLayout = finalLayout_ == VK_IMAGE_LAYOUT_MAX_ENUM ? in.prevLayout : finalLayout_;

	Barriers inBarriers, outBarriers;
	inBarriers.append(in, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	this->barriers(inBarriers);

	VkBufferImageCopy region {
		0, in.extent.width, in.extent.height,
		VkImageSubresourceLayers { in.aspect, 0, 0, 1 },
		VkOffset3D{0,0,0},
		VkExtent3D{in.extent.width,in.extent.height,1},
	};

	//typedef void (VKAPI_PTR *PFN_vkCmdCopyImageToBuffer)(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
	vkCmdCopyImageToBuffer(cmdBuf, in, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out, 1, &region);

	outBarriers.append(in, finalLayout);
	this->barriers(outBarriers);
	return *this;
}
Command& Command::copyBufferToImage(ExImage& out, ExBuffer& in, VkImageLayout finalLayout_) {
	VkImageLayout finalLayout = finalLayout_ == VK_IMAGE_LAYOUT_MAX_ENUM ? out.prevLayout : finalLayout_;

	Barriers inBarriers, outBarriers;
	inBarriers.append(out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	this->barriers(inBarriers);

	VkBufferImageCopy region {
		0, out.extent.width, out.extent.height,
		VkImageSubresourceLayers { out.aspect, 0, 0, 1 },
		VkOffset3D{0,0,0},
		VkExtent3D{out.extent.width,out.extent.height,1},
	};

	//typedef void (VKAPI_PTR *PFN_vkCmdCopyBufferToImage)(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions);
	vkCmdCopyBufferToImage(cmdBuf, in, out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	outBarriers.append(out, finalLayout);
	this->barriers(outBarriers);
	return *this;
}

void Submission::submit(VkCommandBuffer* cmds, uint32_t n, bool block) {
		VkSubmitInfo si {
				VK_STRUCTURE_TYPE_SUBMIT_INFO,
				nullptr,
				(uint32_t)waitSemas.size(), waitSemas.data(),
				waitStages.data(),
				n, cmds,
				(uint32_t)signalSemas.size(), signalSemas.data()
		};

		vkQueueSubmit(q, 1, &si, fence);
		if (block and fence) {
			vkWaitForFences(device, 1, &fence, true, timeout);
			vkResetFences(device, 1, &fence);
		}
}


SwapChain::SwapChain(BaseApp& app) : device(&app.mainDevice) {
}

TheDescriptorPool::TheDescriptorPool(BaseApp& app) : device(&app.mainDevice) {
	std::vector<VkDescriptorPoolSize> sizes(app.cfg.descriptorPoolConfig.poolSizes.size());
	for (int i=0; i<sizes.size(); i++) {
		sizes[i] = VkDescriptorPoolSize {
				(VkDescriptorType)app.cfg.descriptorPoolConfig.poolSizes[i].first,
				(uint32_t)app.cfg.descriptorPoolConfig.poolSizes[i].second,
		};
	}


	VkDescriptorPoolCreateInfo ci;
	ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	ci.pNext = nullptr;
	//ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	ci.flags = {};
	ci.maxSets = app.cfg.descriptorPoolConfig.maxSets;
	ci.poolSizeCount = sizes.size();
	ci.pPoolSizes = sizes.data();

	assertCallVk(vkCreateDescriptorPool(device->device, &ci, nullptr, &pool));
}
TheDescriptorPool::~TheDescriptorPool() {
	if (pool != nullptr) vkDestroyDescriptorPool(*device, pool, {});
}

static VkInstance makeInstance(const AppConfig& cfg, VkDebugUtilsMessengerEXT& debugMessenger) {

	/*
#ifndef VK_HEADER_VERSION_COMPLETE
#ifdef VK_MAKE_API_VERSION
	#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION(0, 1, 3, VK_HEADER_VERSION)
#else VK_MAKE_API_VERSION
	#define VK_HEADER_VERSION_COMPLETE VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION)
#endif
#endif
	*/
	uint32_t desiredApiVersion = VK_HEADER_VERSION_COMPLETE;

	// If using raytracing, we need 1.2+
	//if (rti.enablePipeline) desiredApiVersion = VK_MAKE_VERSION(1,3,0);
	desiredApiVersion = VK_MAKE_VERSION(1,3,0);

	VkApplicationInfo appInfo;
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = nullptr;
	appInfo.pApplicationName = "fvkApp";
	appInfo.applicationVersion = 1;
	appInfo.pEngineName = "fvk";
	appInfo.engineVersion = 1;
	appInfo.apiVersion = desiredApiVersion;

	std::vector<const char*> layers, exts;

	// To turn off:
	//        run with env NO_VULKAN_DEBUG=1
#ifdef VULKAN_DEBUG
	if (getenv("NO_VULKAN_DEBUG") == 0) {
		exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		exts.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		layers.push_back("VK_LAYER_KHRONOS_validation");
	}
#endif



	for (auto ext : cfg.extraInstanceExtensions()) exts.push_back(ext);


	VkInstanceCreateInfo ici;
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pNext = nullptr;

	ici.flags = {};
	ici.pApplicationInfo = &appInfo;
	ici.enabledLayerCount = layers.size();
	ici.ppEnabledLayerNames = layers.data();
	ici.enabledExtensionCount = exts.size();
	ici.ppEnabledExtensionNames = exts.data();

	VkInstance instance;
	assertCallVk(vkCreateInstance(&ici, nullptr, &instance));

	uint32_t instanceVersion;
	vkEnumerateInstanceVersion(&instanceVersion);
	if (VK_API_VERSION_MAJOR(instanceVersion) <= 1 and VK_API_VERSION_MINOR(instanceVersion) < 3) {
		fmt::print(" - instance version was ({} :: {} {} {}), but must be AT LEAST 1.3.x\n", instanceVersion, VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion));
		fmt::print(" - Did you install the vulkan SDK, or if not, does your OS package manager provide a recent enough vulkan loader?\n");
		fmt::print(" - If you installed the SDK, make sure ld is finding it (i.e. LD_CONFIG_PATH includes sdk lib dir)\n");
		fmt::print(" - If the loader is sufficient, is your graphics driver recent enough?\n");
		assert(false and "instance version (vulkan loader) too old");
	}
	fmt::print(" - instance version {} :: {} {} {}\n", instanceVersion, VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion));

#ifdef VULKAN_DEBUG
	if (getenv("NO_VULKAN_DEBUG") == 0) {
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback_;
		createInfo.pUserData = nullptr; // Optional

		// VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (func != nullptr) {
			func(instance, &createInfo, nullptr, &debugMessenger);
		} else {
			//return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
#endif

	return instance;
}

// This relies on NVRO
static Device createStandardDevice(const AppConfig& cfg, VkInstance instance) {
	return makeGpuDeviceAndQueues(cfg, instance);
	//return Device{};
}

BaseApp::BaseApp(const AppConfig& cfg_)
	:   cfg(cfg_)
{

}

BaseApp::~BaseApp() {
	fmt::print(" - ~BaseApp()\n");
	if (debugMessenger) {
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func)
			func(instance, debugMessenger, nullptr);
	}
	destroyFrameDatas();
	if (window) {
		delete window;
	}
}


FrameData& BaseApp::acquireNextFrame() {
//typedef VkResult (VKAPI_PTR *PFN_vkAcquireNextImageKHR)(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);

	FrameData& fd = frameDatas[frameIdx];

	if (swapchain.displaySwapchain) {
		vkAcquireNextImageKHR(mainDevice, swapchain.displaySwapchain, 99999999999lu, nullptr, fd.frameAvailableFence, &fd.scIndx);
		// vkAcquireNextImageKHR(mainDevice, swapchain.displaySwapchain, 99999999999lu, fd.swapchainAcquireSema, fd.frameAvailableFence, &fd.scIndx);

		// TODO I don't think you need this
		if (1) {
			vkWaitForFences(mainDevice, 1, &fd.frameAvailableFence, true, 9999999999);
			vkResetFences(mainDevice, 1, &fd.frameAvailableFence);
		}
	} else {
		fd.scIndx++;
		if (fd.scIndx >= swapchain.numImages) fd.scIndx = 0;

		if (1) {
			// vkWaitForFences(mainDevice, 1, &fd.frameAvailableFence, true, 9999999999);
			// vkResetFences(mainDevice, 1, &fd.frameAvailableFence);
		}
	}

	// Spec seemed to indicate after acquire img will be presentSrcKhr, but actuall is undefined...
	// fd.swapchainImg.setFromSwapchain(mainDevice, swapchain.images[fd.scIndx], swapchain.size, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT, swapchain.surfFormat.format);
	// fd.swapchainImg(swapchain).setFromSwapchain(mainDevice, swapchain.images[fd.scIndx], swapchain.imageViews[fd.scIndx], swapchain.size, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, swapchain.surfFormat.format);
	fd.swapchainImg = &swapchain.images[fd.scIndx];
	//fd.swapchainImg->setFromSwapchain(mainDevice, swapchain.images[fd.scIndx].img, swapchain.images[fd.scIndx].view, swapchain.size, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, swapchain.surfFormat.format);
	// fmt::print(" - using frameDatas[{}] (scIndx {}) fence {}, img {}\n", frameIdx, fd.scIndx, (void*)fd.frameAvailableFence, (void*)fd.swapchainImg->img);


	frameIdx++;
	if (frameIdx >= frameOverlap) frameIdx = 0;

	return fd;
}

void BaseApp::render() {
	assert(camera);

	if (window) window->pollWindowEvents();

	if (isDone() or frameDatas.size() == 0) {
		return;
	}

	FrameData& fd = acquireNextFrame();

	fd.time = getSeconds(renders==0);
	if (renders == 0) time0 = fd.time;
	fd.dt = fd.time - lastTime;
	fd.n = renders++;

	if (renders > 2)
		fpsMeter = fpsMeter * .95 + .05 * (1. / fd.dt);
	else
		fpsMeter = 1./fd.dt;

	// fmt::print(" - [BaseApp::render] time {}\n", fd.time);

	// Step the camera with a smoothed dt
	if (fpsMeter > .0000001) camera->step(1.0 / fpsMeter);
	else camera->step(0.);
	lastTime = fd.time;

	// Update Camera Buffer
	if (1) {
		renderState.camera = camera.get();
		renderState.frameBegin(&fd);
	}

	fd.cmd.begin();
	doRender(renderState);


	DeviceQueueSpec dqs { mainDevice, mainQueue };
	executeAndPresent(renderState, fd);

	postRender();
}

void BaseApp::initVk() {

		// instance(makeInstance(cfg)),
		// mainDevice(createStandardDevice(cfg, instance)),
		// mainQueue(mainDevice, mainDevice.queueFamilyGfxIdxs[0], 0),
		// dpool(*this), swapchain(*this),
		// mainCommandPool(mainDevice, mainDevice.queueFamilyGfxIdxs[0])
	instance = makeInstance(cfg, debugMessenger);
	mainDevice = std::move(createStandardDevice(cfg, instance));
	mainQueue = std::move(Queue{mainDevice, mainDevice.queueFamilyGfxIdxs[0], 0});
	dpool = std::move(TheDescriptorPool(*this));
	swapchain = std::move(SwapChain(*this));
	mainCommandPool = std::move(CommandPool{mainDevice, mainDevice.queueFamilyGfxIdxs[0]});

	windowWidth = cfg.width;
	windowHeight = cfg.height;

	if (cfg.windowSys == AppConfig::WindowSys::eGlfw) {
		fmt::print(" - [BaseApp::initVk] Selected windowSys was eGlfw, making GLFW window and 'real' swapchain\n");
		makeGlfwWindow();
		makeRealSwapChain();
	} else if (cfg.windowSys == AppConfig::WindowSys::eHeadless) {
		fmt::print(" - [BaseApp::initVk] Selected windowSys was eHeadless, making 'fake' swapchain\n");
		window = new MyHeadlessWindow(windowHeight,windowHeight);
		window->setupWindow();
		makeFakeSwapChain();
	}

	makeFrameDatas();
	make_basic_render_stuff();

	generalUploader.create(&mainDevice, &mainQueue);
}

void SimpleRenderPass::begin(Command& cmd, FrameData& fd) {
	VkClearValue clearValues[2] = {
		VkClearValue { .color = VkClearColorValue { .float32 = {0.f,0.f,0.f,0.f} } },
		VkClearValue { .depthStencil = VkClearDepthStencilValue { 1.f, 0 } }
	};

	// clearValues[0].color.float32[0] = 1.f;

	VkRenderPassBeginInfo bi { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	bi.renderPass = pass;
	bi.framebuffer = framebuffers[fd.scIndx];
	bi.renderArea = VkRect2D { VkOffset2D{0,0}, VkExtent2D{framebufferWidth,framebufferHeight} };
	bi.clearValueCount = clearCount;
	bi.pClearValues = clearValues;
	vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void SimpleRenderPass::beginWithExternalFbo(Command& cmd, FrameData& fd, VkFramebuffer fbo) {
	VkClearValue clearValues[2] = {
		VkClearValue { .color = VkClearColorValue { .float32 = {0.f,0.f,0.f,0.f} } },
		VkClearValue { .depthStencil = VkClearDepthStencilValue { 1.f, 0 } }
	};

	VkRenderPassBeginInfo bi { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	bi.renderPass = pass;
	bi.framebuffer = fbo;
	bi.renderArea = VkRect2D { VkOffset2D{0,0}, VkExtent2D{framebufferWidth,framebufferHeight} };
	bi.clearValueCount = clearCount;
	bi.pClearValues = clearValues;
	vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void SimpleRenderPass::end(Command& cmd, FrameData& fd) {
	vkCmdEndRenderPass(cmd);
	fd.swapchainImg->prevLayout = outputLayout;
	if (depthImages.size()) depthImages[fd.scIndx].prevLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

SimpleRenderPass::~SimpleRenderPass() {
	for (int i=0; i<framebuffers.size(); i++) {
		vkDestroyFramebuffer(device, framebuffers[i], nullptr);
	}

	depthImages.clear();

	if (pass) {
		vkDestroyRenderPass(device, pass, nullptr);
	}
}




/*
int main() {

}
*/
