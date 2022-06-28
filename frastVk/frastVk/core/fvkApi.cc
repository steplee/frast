#include "fvkApi.h"

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

Queue::Queue(Device& d, uint32_t family, int idx) {
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

void Command::begin(VkCommandBufferUsageFlags flags) {

	assertCallVk(vkResetCommandBuffer(cmdBuf, {}));

	VkCommandBufferBeginInfo bi;
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.pNext = nullptr;
	bi.flags = {};
	bi.pInheritanceInfo = nullptr;
	assertCallVk(vkBeginCommandBuffer(cmdBuf, &bi));
}
void Command::end() {
	assertCallVk(vkEndCommandBuffer(cmdBuf));
}

void Command::barriers(Barriers& b) {
	if (b.memBarriers.size() or b.bufBarriers.size() or b.imgBarriers.size())
		vkCmdPipelineBarrier(cmdBuf, b.srcStageMask, b.dstStageMask, b.depFlags,
				(uint32_t)b.memBarriers.size(), b.memBarriers.data(),
				(uint32_t)b.bufBarriers.size(), b.bufBarriers.data(),
				(uint32_t)b.imgBarriers.size(), b.imgBarriers.data());
}

void Command::clearImage(ExImage& image, const std::vector<float>& color) {

	// Might have to transition
	if (image.prevLayout != VK_IMAGE_LAYOUT_GENERAL and image.prevLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		Barriers barriers_;
		barriers_.append(image, VK_IMAGE_LAYOUT_GENERAL);
		this->barriers(barriers_);
	}

	VkImageSubresourceRange range { image.aspect, 0, 1, 0, 1};
	vkCmdClearColorImage(cmdBuf, image.img, image.prevLayout, (VkClearColorValue*)color.data(), 1, &range);
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

static VkInstance makeInstance(const AppConfig& cfg) {

	uint32_t desiredApiVersion = VK_HEADER_VERSION_COMPLETE;
	// If using raytracing, we need 1.2+
	//if (rti.enablePipeline) desiredApiVersion = VK_MAKE_VERSION(1,3,0);

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
	return instance;
}

// This relies on NVRO
static Device createStandardDevice(const AppConfig& cfg, VkInstance instance) {
	return makeGpuDeviceAndQueues(cfg, instance);
	//return Device{};
}

BaseApp::BaseApp(const AppConfig& cfg)
	:   cfg(cfg)
{

}

BaseApp::~BaseApp() {
	fmt::print(" - ~BaseApp()\n");
	destroyFrameDatas();
	if (glfwWindow) {
		glfwWindow->destroyWindow();
		delete glfwWindow;
	}
}


FrameData& BaseApp::acquireNextFrame() {
//typedef VkResult (VKAPI_PTR *PFN_vkAcquireNextImageKHR)(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);

	FrameData& fd = frameDatas[frameIdx];

	vkAcquireNextImageKHR(mainDevice, swapchain.displaySwapchain, 99999999999lu, fd.swapchainAcquireSema, fd.frameAvailableFence, &fd.scIndx);

	// Spec seemed to indicate after acquire img will be presentSrcKhr, but actuall is undefined...
	// fd.swapchainImg.setFromSwapchain(mainDevice, swapchain.images[fd.scIndx], swapchain.size, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT, swapchain.surfFormat.format);
	// fd.swapchainImg(swapchain).setFromSwapchain(mainDevice, swapchain.images[fd.scIndx], swapchain.imageViews[fd.scIndx], swapchain.size, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, swapchain.surfFormat.format);
	fd.swapchainImg = &swapchain.images[fd.scIndx];
	//fd.swapchainImg->setFromSwapchain(mainDevice, swapchain.images[fd.scIndx].img, swapchain.images[fd.scIndx].view, swapchain.size, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT, swapchain.surfFormat.format);
	fmt::print(" - using frameDatas[{}] (scIndx {}) fence {}, img {}\n", frameIdx, fd.scIndx, (void*)fd.frameAvailableFence, (void*)fd.swapchainImg->img);

	// TODO I don't think you need this
	if (1) {
		vkWaitForFences(mainDevice, 1, &fd.frameAvailableFence, true, 9999999999);
		vkResetFences(mainDevice, 1, &fd.frameAvailableFence);
	}

	frameIdx++;
	if (frameIdx >= frameOverlap) frameIdx = 0;

	return fd;
}

void BaseApp::render() {
	assert(camera);

	if (isDone() or frameDatas.size() == 0) {
		return;
	}

	FrameData& fd = acquireNextFrame();
	if (fpsMeter > .0000001) camera->step(1.0 / fpsMeter);

	// Update Camera Buffer
	if (1) {
		renderState.camera = camera.get();
		renderState.frameBegin(&fd);
	}

	doRender(renderState);


	DeviceQueueSpec dqs { mainDevice, mainQueue };
	fd.cmd.executeAndPresent(dqs, swapchain, fd);

	postRender();
}

void BaseApp::initVk() {

		// instance(makeInstance(cfg)),
		// mainDevice(createStandardDevice(cfg, instance)),
		// mainQueue(mainDevice, mainDevice.queueFamilyGfxIdxs[0], 0),
		// dpool(*this), swapchain(*this),
		// mainCommandPool(mainDevice, mainDevice.queueFamilyGfxIdxs[0])
	instance = makeInstance(cfg);
	mainDevice = std::move(createStandardDevice(cfg, instance));
	mainQueue = std::move(Queue{mainDevice, mainDevice.queueFamilyGfxIdxs[0], 0});
	dpool = std::move(TheDescriptorPool(*this));
	swapchain = std::move(SwapChain(*this));
	mainCommandPool = std::move(CommandPool{mainDevice, mainDevice.queueFamilyGfxIdxs[0]});

	if (cfg.windowSys == AppConfig::WindowSys::eGlfw) {
		makeGlfwWindow();
		makeRealSwapChain();
	}

	makeFrameDatas();
	make_basic_render_stuff();
}

void SimpleRenderPass::begin(Command& cmd, FrameData& fd) {
	VkClearValue clearValues[2] = {
		VkClearValue { .color = VkClearColorValue { .float32 = {0.f,0.f,0.f,0.f} } },
		VkClearValue { .depthStencil = VkClearDepthStencilValue { 0.f, 0 } }
	};

	// clearValues[0].color.float32[0] = 1.f;

	VkRenderPassBeginInfo bi { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
	bi.renderPass = pass;
	bi.framebuffer = framebuffers[fd.scIndx];
	bi.renderArea = VkRect2D { VkOffset2D{0,0}, VkExtent2D{framebufferWidth,framebufferHeight} };
	bi.clearValueCount = 2;
	bi.pClearValues = clearValues;
	vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void SimpleRenderPass::end(Command& cmd, FrameData& fd) {
	vkCmdEndRenderPass(cmd);
	fd.swapchainImg->prevLayout = outputLayout;
}




/*
int main() {

}
*/
