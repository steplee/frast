#include "fvkApi.h"
#include <cassert>

uint64_t scalarSizeOfFormat(const VkFormat& f) {
	switch (f) {
		case VK_FORMAT_D32_SFLOAT: return 4*1;
		case VK_FORMAT_R32_SFLOAT: return 4*1;
		case VK_FORMAT_R32G32_SFLOAT: return 4*2;
		case VK_FORMAT_R32G32B32_SFLOAT: return 4*3;
		case VK_FORMAT_R32G32B32A32_SFLOAT: return 4*4;

		case VK_FORMAT_R64_SFLOAT: return 8*1;
		case VK_FORMAT_R64G64_SFLOAT: return 8*2;
		case VK_FORMAT_R64G64B64_SFLOAT: return 8*3;
		case VK_FORMAT_R64G64B64A64_SFLOAT: return 8*4;

		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_USCALED:
											  return 1;
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R8G8_SINT:
		case VK_FORMAT_R8G8_UNORM:
		case VK_FORMAT_R8G8_SNORM:
		case VK_FORMAT_R8G8_SSCALED:
		case VK_FORMAT_R8G8_USCALED:
											  return 2;
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SNORM:
		case VK_FORMAT_R8G8B8_SSCALED:
		case VK_FORMAT_R8G8B8_USCALED:
											  return 3;
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_USCALED:
											  return 4;
		default: {
					 // throw std::runtime_error("[scalarSizeOfFormat] Unsupported format " + vk::to_string(f));
					 throw std::runtime_error("[scalarSizeOfFormat] Unsupported format " + std::to_string(f));
				 }
	}
	return 0;
}

uint32_t findMemoryTypeIndex(const VkPhysicalDevice& pdev, const VkMemoryPropertyFlags& flags, uint32_t maskOrZero) {
	VkPhysicalDeviceMemoryProperties props;
	vkGetPhysicalDeviceMemoryProperties(pdev, &props);

	for (int i=0; i<props.memoryTypeCount; i++) {
		if ( ((maskOrZero == 0) or ((1<<i) & maskOrZero)) )
			if ((props.memoryTypes[i].propertyFlags & flags) == flags) return i;
			// if ((props.memoryTypes[i].propertyFlags & flags)) return i;
	}
	throw std::runtime_error("failed to find memory type index.");
	return 99999999;
}

Device makeGpuDeviceAndQueues(const AppConfig& cfg, VkInstance instance) {
	uint32_t ndevices = 8;
	VkPhysicalDevice pdevices[8];
	vkEnumeratePhysicalDevices(instance, &ndevices, pdevices);

	// Pick first device that amongst class of (discrete/integrated/cpu)
	uint32_t myDeviceIdx = 99999;
	for (auto prefer : {
			VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
			VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
			VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
			VK_PHYSICAL_DEVICE_TYPE_CPU })
		for (int i=0; myDeviceIdx>9999 and i<ndevices; i++) {
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(pdevices[i], &props);
			if (props.deviceType != prefer) {
				printf(" - want %u, skipping dev %d of type %u\n", prefer, i, props.deviceType);
			} else {
				/*
				if (rti.enablePipeline) {
					vk::PhysicalDeviceFeatures2 feats;
					feats.pNext = &rti.accFeatures;
					rti.accFeatures.pNext = &rti.rayPiplelineFeatures;
					vkGetPhysicalDeviceFeatures2(pdevices[i], (VkPhysicalDeviceFeatures2*)&feats);

					if (not rti.accFeatures.accelerationStructure) {
						printf(" - skipping dev %d (ok type %u), but does not support accelerationStructure\n", i, props.deviceType);
						continue;
					}
					if (not rti.accFeatures.accelerationStructureHostCommands) {
						// printf(" - skipping dev %d (ok type %u), but does not support accelerationStructureHostCommands\n", i, props.deviceType);
						// continue;
					}
					if (not rti.rayPiplelineFeatures.rayTracingPipeline) {
						printf(" - skipping dev %d (ok type %u), but does not support rayTracingPipeline\n", i, props.deviceType);
						continue;
					}
				}
				if (rti.enableQuery) {
					assert((not rti.enableQuery) && "rayQuery not supported now");
				}

				VkFormatProperties2 fprops;
				fprops.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES2;
				vkGetPhysicalDeviceFormatProperties2(pdevices[i], VK_FORMAT_R8G8B8_UINT, (VkFormatProperties2*)&fprops);
				fmt::print(" - BuildAccelStructure supports uint8   : {}\n", (uint32_t)(fprops.formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eAccelerationStructureVertexBufferKHR));
				vkGetPhysicalDeviceFormatProperties2(pdevices[i], (VkFormat)vk::Format::eR8G8B8Sint, (VkFormatProperties2*)&fprops);
				fmt::print(" - BuildAccelStructure supports  int8   : {}\n", (uint32_t)(fprops.formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eAccelerationStructureVertexBufferKHR));
				vkGetPhysicalDeviceFormatProperties2(pdevices[i], (VkFormat)vk::Format::eR8G8B8Unorm, (VkFormatProperties2*)&fprops);
				fmt::print(" - BuildAccelStructure supports unorm8  : {}\n", (uint32_t)(fprops.formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eAccelerationStructureVertexBufferKHR));
				vkGetPhysicalDeviceFormatProperties2(pdevices[i], (VkFormat)vk::Format::eR8G8B8Snorm, (VkFormatProperties2*)&fprops);
				fmt::print(" - BuildAccelStructure supports  norm8  : {}\n", (uint32_t)(fprops.formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eAccelerationStructureVertexBufferKHR));
				vkGetPhysicalDeviceFormatProperties2(pdevices[i], (VkFormat)vk::Format::eR32G32B32Sfloat, (VkFormatProperties2*)&fprops);
				fmt::print(" - BuildAccelStructure supports float32 : {}\n", (uint32_t)(fprops.formatProperties.bufferFeatures & vk::FormatFeatureFlagBits::eAccelerationStructureVertexBufferKHR));
				*/

				myDeviceIdx = i;
				break;
			}
		}
	if (myDeviceIdx >= 999) {
		throw std::runtime_error(" - No device could be found (did you enable features not supported?)");
	}
	VkPhysicalDevice pdeviceGpu = pdevices[myDeviceIdx];
	//pdeviceGpu = vk::raii::PhysicalDevice { instance, pdevice };

	// Find gfx enabled queue
	uint32_t nqprops = 32;
	VkQueueFamilyProperties qprops[32];
	vkGetPhysicalDeviceQueueFamilyProperties(pdeviceGpu, &nqprops, qprops);
	uint32_t qfamilyIdx = 99999;
	for (int i=0; i<nqprops; i++) {
		if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			qfamilyIdx = i;
			break;
		}
	}
	assert(qfamilyIdx < 9999);

	VkPhysicalDeviceProperties pdeviceProps;
	vkGetPhysicalDeviceProperties(pdeviceGpu, &pdeviceProps);
	printf(" - Selected Device '%s', queueFamily %u.\n", pdeviceProps.deviceName, qfamilyIdx);
	std::vector<uint32_t> queueFamilyGfxIdxs;
	queueFamilyGfxIdxs.push_back(qfamilyIdx);

	// Create Device
	// Two levels of queue creation:
	//    1) Each DeviceQueueCreateInfo specifies one family, and a number to create in that one family.
	//    2) The DeviceCreateInfo allows multiple such families.
	constexpr uint32_t n_q_family = 1;
	uint32_t qcnt = 2;
	std::vector<float> qprior;
	for (int i=0; i<qcnt; i++) qprior.push_back(1.f);


	VkDeviceQueueCreateInfo qinfos[n_q_family] = {{
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		nullptr, {},
		qfamilyIdx, qcnt, qprior.data()

	}};


	// -----------------------------------------------
	//      Extensions
	// -----------------------------------------------

	std::vector<const char*> exts = {
		VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
		VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME
		// VK_KHR_SURFACE_EXTENSION_NAME,
		// VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		// ,VK_EXT_MULTI_DRAW_EXTENSION_NAME  
		//,VK_KHR_16BIT_STORAGE_EXTENSION_NAME
	};

	for (auto ext : cfg.extraDeviceExtensions()) exts.push_back(ext);

	/*
	std::vector<vk::ExtensionProperties> availableExts = pdeviceGpu.enumerateDeviceExtensionProperties();

	for (auto & aext : availableExts) {
		if (strcmp(aext.extensionName.data(), VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0)
			rti.haveDeferredHostOps = true;
		if (strcmp(aext.extensionName.data(), VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME) == 0)
			rti.havePipelineLibrary = true;
	}

	if (rti.enablePipeline or rti.enableQuery) {
		// These are unconditionally required
		// They are gauranteed to be supported since we already checked the PhysicalDeviceFeatures
		exts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
		exts.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
		exts.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
		exts.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
		exts.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
		// exts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

		// These are optional
		if (rti.haveDeferredHostOps)
			exts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
		if (rti.havePipelineLibrary)
			exts.push_back(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
	}
	if (rti.enableQuery) {
		// TODO
	}
	*/


	using S = VkBaseInStructure;
	S *_prev = nullptr, *_first = nullptr;

	// Helper function that makes the next chain simple
	auto pushIt = [&_prev, &_first](VkBaseInStructure* ptr) {
		if (not _first) _first = ptr;
		if (_prev) _prev->pNext = ptr;
		_prev = ptr;
	};


	// -----------------------------------------------
	//      Basic Features
	// -----------------------------------------------

	VkPhysicalDeviceVulkan11Features extraFeatures3;
	memset(&extraFeatures3, 0, sizeof(decltype(extraFeatures3)));
	extraFeatures3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	extraFeatures3.storageBuffer16BitAccess = true;
	extraFeatures3.uniformAndStorageBuffer16BitAccess = true;
    extraFeatures3.pNext = nullptr;
	pushIt((S*)&extraFeatures3);

	VkPhysicalDeviceFeatures2 extraFeatures4;
	memset(&extraFeatures4, 0, sizeof(decltype(extraFeatures4)));
	extraFeatures4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	extraFeatures4.features.shaderInt16 = true;
    extraFeatures4.pNext = nullptr;
	pushIt((S*)&extraFeatures4);

	VkPhysicalDeviceIndexTypeUint8FeaturesEXT extraFeatures5 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
		nullptr,
		true
	};
	pushIt((S*)&extraFeatures5);

	VkPhysicalDeviceMultiDrawFeaturesEXT extraFeatures6;
	memset(&extraFeatures6, 0, sizeof(decltype(extraFeatures6)));
	extraFeatures6.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT;
    extraFeatures6.pNext = nullptr;
	extraFeatures6.multiDraw = true;
	pushIt((S*)&extraFeatures6);

	VkPhysicalDeviceRobustness2FeaturesEXT extraFeatures7;
	memset(&extraFeatures7, 0, sizeof(decltype(extraFeatures7)));
	extraFeatures7.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    extraFeatures7.pNext = nullptr;
    extraFeatures7.nullDescriptor = true;
	pushIt((S*)&extraFeatures7);

	VkPhysicalDeviceVulkan12Features extraFeatures8;
	memset(&extraFeatures8, 0, sizeof(VkPhysicalDeviceVulkan12Features));
    extraFeatures8.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    extraFeatures8.pNext = nullptr;
	extraFeatures8.shaderInt8 = true;
	extraFeatures8.uniformBufferStandardLayout = true;
	extraFeatures8.uniformAndStorageBuffer8BitAccess = true;
	extraFeatures8.storageBuffer8BitAccess = true;
	extraFeatures8.bufferDeviceAddress = true;
	pushIt((S*)&extraFeatures8);


	// -----------------------------------------------
	//      RayTracing Features
	// -----------------------------------------------

	/*
	if (rti.enablePipeline) {
		pushIt((S*)&rti.accFeatures);
		pushIt((S*)&rti.rayPiplelineFeatures);
	}
	if (rti.enableQuery) {
		assert((not rti.enableQuery) && "rayQuery not supported now");
	}
	*/

	// -----------------------------------------------
	//      Ready to make device!
	// -----------------------------------------------

	fmt::print(" - Final Device Extensions: "); for (auto s : exts) fmt::print("{}, ", s); fmt::print("\n");

	VkDevice deviceGpu;
    VkDeviceCreateInfo dinfo {
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			_first,
			{},
			n_q_family, qinfos,
			0, nullptr,
			(uint32_t)exts.size(), exts.data(),
			nullptr };
	//deviceGpu = std::move(vk::raii::Device(pdeviceGpu, dinfo, nullptr));

	//auto stat = vkCreateDevice(pdeviceGpu, &dinfo, nullptr, &deviceGpu);
	//assert(VK_SUCCESS == stat);
	assertCallVk(vkCreateDevice(pdeviceGpu, &dinfo, nullptr, &deviceGpu));

	fmt::print(" - created device {}\n", (void*)deviceGpu);

	return Device{
		deviceGpu, pdeviceGpu, queueFamilyGfxIdxs
	};

	/*
	uint32_t instanceVersion = ctx.enumerateInstanceVersion();
	// std::cout << " - pdevice api version: " << pdeviceProps.apiVersion << "\n";
	fmt::print(" - instance version {} :: {} {} {}\n", instanceVersion, VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion));
	fmt::print(" - pdevice api version {} :: {} {} {}\n", pdeviceProps.apiVersion, VK_API_VERSION_MAJOR(pdeviceProps.apiVersion), VK_API_VERSION_MINOR(pdeviceProps.apiVersion), VK_API_VERSION_PATCH(pdeviceProps.apiVersion));
	std::cout << " - device vk header version: " << deviceGpu.getDispatcher()->getVkHeaderVersion() << " header " << VK_HEADER_VERSION << "\n";
	*/

	// Get Queue
	//queueGfx = deviceGpu.getQueue(qfamilyIdx, 0);



	/*
	if (rti.enablePipeline) {
		VkPhysicalDeviceProperties2 deviceProperties2{};
		deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		deviceProperties2.pNext = &rti.rayPipelineProps;
		vkGetPhysicalDeviceProperties2(*pdeviceGpu, &deviceProperties2);
	}
	*/
}




void BaseApp::makeGlfwWindow() {
	glfwWindow = new Window(cfg.height, cfg.width, false);
	glfwWindow->setupWindow();
}

void BaseApp::makeRealSwapChain() {
	// Make surface
	auto res = glfwCreateWindowSurface(instance, glfwWindow->glfwWindow, NULL, &swapchain.surface);

	// Select swapchain details

	VkSurfaceFormatKHR surfFormat;
	{
		uint32_t formats_count;
		if( (vkGetPhysicalDeviceSurfaceFormatsKHR( mainDevice.pdevice, swapchain.surface, &formats_count, nullptr ) != VK_SUCCESS) ||
				(formats_count == 0) )
			throw std::runtime_error("failed to enumerate surface formats (or # = 0)");
		std::vector<VkSurfaceFormatKHR> surface_formats( formats_count );
		if( vkGetPhysicalDeviceSurfaceFormatsKHR( mainDevice.pdevice, swapchain.surface, &formats_count, &surface_formats[0] ) != VK_SUCCESS )
			throw std::runtime_error("failed to enumerate surface formats");

		// None preferred: we can choose.
		if( (surface_formats.size() == 1) &&
				(surface_formats[0].format == VK_FORMAT_UNDEFINED) ) {
			surfFormat = { VK_FORMAT_R8G8B8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR };
		} else {
			// Check if list contains most widely used R8 G8 B8 A8 format
			// with nonlinear color space
			bool set = false;
			for( VkSurfaceFormatKHR &surface_format : surface_formats )
				if( surface_format.format == VK_FORMAT_R8G8B8A8_UNORM ) {
					surfFormat = surface_format;
					set = true;
				}
			if (not set)
				for( VkSurfaceFormatKHR &surface_format : surface_formats )
					if( surface_format.format == VK_FORMAT_R8G8B8A8_UINT ) {
						surfFormat = surface_format;
						set = true;
					}
			if (not set)
				for( VkSurfaceFormatKHR &surface_format : surface_formats )
					if( surface_format.format == VK_FORMAT_B8G8R8A8_UNORM) {
						surfFormat = surface_format;
						set = true;
					}
			if (not set) {
				for( VkSurfaceFormatKHR &surface_format : surface_formats )
					printf(" - (PICK ONE) format: fmt %u\n", surface_format.format);
				throw std::runtime_error(" - SurfFormat not set");
			}
		}
	}
	swapchain.surfFormat = VkSurfaceFormatKHR { surfFormat };

	// See https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VkPresentModeKHR.html
	// presentMode = vk::PresentModeKHR::eFifo;
	// presentMode = vk::PresentModeKHR::eImmediate;
	swapchain.presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;

	VkSwapchainCreateInfoKHR sc_ci_ {
		VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		{},
		{},
		swapchain.surface,
		3,
		swapchain.surfFormat.format,                        // VkFormat                       imageFormat
		swapchain.surfFormat.colorSpace,                    // VkColorSpaceKHR                imageColorSpace
		VkExtent2D{cfg.width,cfg.height},
		1,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SHARING_MODE_EXCLUSIVE,
		0,
		nullptr,
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		swapchain.presentMode,
		VK_TRUE, nullptr
	};
	swapchain.numImages = 3;

	swapchain.images.resize(swapchain.numImages);

	assertCallVk(vkCreateSwapchainKHR(mainDevice.device, &sc_ci_, nullptr, &swapchain.displaySwapchain));
	fmt::print(" - created swapchain with {} images\n", swapchain.numImages);

	vkGetSwapchainImagesKHR(mainDevice, swapchain.displaySwapchain, &swapchain.numImages, swapchain.images.data());

	swapchain.imageViews.resize(swapchain.numImages);
	for (int i=0; i<swapchain.numImages; i++) {
		VkImageViewCreateInfo viewInfo {
			VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr,
			{},
			swapchain.images[i],
			VK_IMAGE_VIEW_TYPE_2D,
			surfFormat.format,
			{},
			VkImageSubresourceRange {
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1, 0, 1 }
		};
		// swapchain.imageViews.push_back(std::move(deviceGpu.createImageView(viewInfo)));
		assertCallVk(vkCreateImageView(mainDevice, &viewInfo, nullptr, &swapchain.imageViews[i]));
	}
}

void BaseApp::makeFrameDatas() {
	/*
	VkFence frameDoneFence;
	VkFence frameAvailableFence;
	VkSemaphore swapchainAcquireSema;
	VkSemaphore renderCompleteSema;

	VkCommandBuffer cmd { nullptr };
	uint32_t scIndx=0, n=0;
	float time=0, dt=0;
	*/

	frameDatas.resize(frameOverlap);

	std::vector<Command> cmds = mainCommandPool.allocate(frameOverlap);

	fmt::print(" - Creating {} frameDatas\n", frameOverlap);
	for (int i=0; i<frameDatas.size(); i++) {
		auto& fd = frameDatas[i];

		VkFenceCreateInfo fci1 { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, {} };
		// VkFenceCreateInfo fci2 { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
		VkFenceCreateInfo fci2 { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, {} };
		assertCallVk(vkCreateFence(mainDevice, &fci1, nullptr, &fd.frameDoneFence));
		assertCallVk(vkCreateFence(mainDevice, &fci2, nullptr, &fd.frameAvailableFence));
		fmt::print(" - frameDatas[{}] fence {}\n", i, (void*)fd.frameAvailableFence);

		VkSemaphoreCreateInfo sci { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, {} };
		assertCallVk(vkCreateSemaphore(mainDevice, &sci, nullptr, &fd.swapchainAcquireSema));
		assertCallVk(vkCreateSemaphore(mainDevice, &sci, nullptr, &fd.renderCompleteSema));

		fd.scIndx = 0;
		fd.n = 0;
		fd.dt = 0;
		fd.time = 0;
		fd.cmd = cmds[i];
	}

// typedef VkResult (VKAPI_PTR *PFN_vkCreateSemaphore)(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
// typedef void (VKAPI_PTR *PFN_vkDestroySemaphore)(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator);

// typedef VkResult (VKAPI_PTR *PFN_vkCreateFence)(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence);
}
void BaseApp::destroyFrameDatas() {

	// If frameOverlap > 1, you need some synchronoization here, or just use vkDeviceWaitIdle()
	/*
	for (int i=0; i<frameDatas.size(); i++) {
		auto& fd = frameDatas[i];
		vkWaitForFences(mainDevice, 1, &fd.frameDoneFence, true, 9999999999);
	}
	*/

	for (int i=0; i<frameDatas.size(); i++) {
		auto& fd = frameDatas[i];
		vkDestroyFence(mainDevice, fd.frameDoneFence, nullptr);
		vkDestroyFence(mainDevice, fd.frameAvailableFence, nullptr);
		vkDestroySemaphore(mainDevice, fd.swapchainAcquireSema, nullptr);
		vkDestroySemaphore(mainDevice, fd.renderCompleteSema, nullptr);
	}
	frameDatas.clear();
// typedef void (VKAPI_PTR *PFN_vkDestroyFence)(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator);
}

bool BaseApp::make_basic_render_stuff() {
	// Create renderpass and framebuffers

	simpleRenderPass.device = mainDevice;

	uint32_t width = cfg.width;
	uint32_t height = cfg.height;

	// Color
	VkAttachmentDescription colorAttachment {
			{},

			swapchain.surfFormat.format,

			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkAttachmentReference colorAttachmentRef {
		0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	// Depth
	for (int i=0; i<swapchain.numImages; i++) {
		ExImage dimg;
		dimg.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		dimg.extent = VkExtent2D { width, height };
		dimg.format = VK_FORMAT_D32_SFLOAT;
		dimg.create(mainDevice);

		//dimg.create(deviceGpu, *pdeviceGpu);
		simpleRenderPass.depthImages.push_back(std::move(dimg));
	}
	VkAttachmentDescription depthAttachment {
			{},
			VkFormat { simpleRenderPass.depthImages[0].format },

			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkAttachmentReference depthAttachmentRef {
		1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	std::vector<VkSubpassDescription> subpasses {
		VkSubpassDescription{
			{},
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				0, nullptr,
				1, &colorAttachmentRef,
				nullptr,
				nullptr,
				0, nullptr }
	};


	// Depth dependency
      /*SubpassDependency( uint32_t                                 srcSubpass_      = {},
                         uint32_t                                 dstSubpass_      = {},
                         VULKAN_HPP_NAMESPACE::PipelineStageFlags srcStageMask_    = {},
                         VULKAN_HPP_NAMESPACE::PipelineStageFlags dstStageMask_    = {},
                         VULKAN_HPP_NAMESPACE::AccessFlags        srcAccessMask_   = {},
                         VULKAN_HPP_NAMESPACE::AccessFlags        dstAccessMask_   = {},
                         VULKAN_HPP_NAMESPACE::DependencyFlags    dependencyFlags_ = {} ) VULKAN_HPP_NOEXCEPT*/
	std::vector<VkSubpassDependency> dependencies = {
		// Depth
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			{},
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			{}
		},
		// 0-1
		/*
		{
			0, 1,
			vk::PipelineStageFlagBits::eAllGraphics,
			vk::PipelineStageFlagBits::eAllGraphics,
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::DependencyFlagBits::eDeviceGroup
		}
		*/
	};

	VkAttachmentDescription atts[2] = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo rpInfo {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr,
		{},
		2, atts,
		(uint32_t)subpasses.size(), subpasses.data(),
		(uint32_t)dependencies.size(), dependencies.data()
	};
	assertCallVk(vkCreateRenderPass(mainDevice, &rpInfo, nullptr, &simpleRenderPass.pass));

	for (int i=0; i<swapchain.numImages; i++) {

		// VkImageView views[2] = { getSwapChainImageView(i), *simpleRenderPass.depthImages[i].view };
		VkImageView views[2] = { swapchain.imageViews[i], simpleRenderPass.depthImages[i].view };

		VkFramebufferCreateInfo fbInfo {
			VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr,
			{},
			simpleRenderPass.pass,
			2,
			views,
			width, height,
			1
		};
		//simpleRenderPass.framebuffers.push_back(std::move(deviceGpu.createFramebuffer(fbInfo)));
	}

	printf(" - Created %zu framebuffers.\n", simpleRenderPass.framebuffers.size());

	return false;
}





void* ExBuffer::map(VkDeviceSize offset, VkDeviceSize size) {
	assert(not mappedAddr);
	assertCallVk(vkMapMemory(device, mem, offset, size, {}, &mappedAddr));
	return mappedAddr;
}
void ExBuffer::unmap() {
	assert(mappedAddr);
	vkUnmapMemory(device, mem);
	mappedAddr = nullptr;
}
void* ExImage::map() {
	assert(not mappedAddr);
	assertCallVk(vkMapMemory(device, mem, 0, capacity, {}, &mappedAddr));
	return mappedAddr;
}
void ExImage::unmap() {
	assert(mappedAddr);
	vkUnmapMemory(device, mem);
	mappedAddr = nullptr;
}

void ExImage::setFromSwapchain(VkDevice device, VkImage& img, VkImageView& view, const VkExtent2D& ex, const VkImageLayout& layout, const VkImageAspectFlags& flags, const VkFormat& fmt) {
	own = false;
	device = device;
	this->img = img;
	this->view = view;
	extent = ex;
	prevLayout = layout;
	aspect = flags;
	format = fmt;
}

void ExImage::create(Device& theDevice) {
	device = theDevice;
	assert(img == nullptr);
	assert(device);
	assert(extent.width != 0);
	assert(extent.height != 0);

	auto d = theDevice.device;
	auto pd = theDevice.pdevice;

	prevLayout = VK_IMAGE_LAYOUT_UNDEFINED;;

	// Image
	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = VkExtent3D{extent.width, extent.height,1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usageFlags;

	vkCreateImage(d, &imageInfo, nullptr, &img);

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(device, img, &reqs);

	uint64_t size_ = reqs.size;
	// auto memPropFlags_ = memPropFlags | ((vk::Flags<vk::MemoryPropertyFlagBits>) reqs.memoryTypeBits);
	uint32_t memMask = reqs.memoryTypeBits;
	auto memPropFlags_ = memPropFlags;

	// Memory
	uint32_t idx = 0;
	uint64_t minSize = std::max(size_, ((size()+0x1000-1)/0x1000)*0x1000);
	idx = findMemoryTypeIndex(pd, memPropFlags_, memMask);
	// printf(" - creating image buffers to memory type idx %u\n", idx);
	VkMemoryAllocateInfo allocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, std::max(minSize,size()), idx };
	//mem = std::move(vk::raii::DeviceMemory(d, allocInfo));
	assertCallVk(vkAllocateMemory(d, &allocInfo, nullptr, &mem));

	assertCallVk(vkBindImageMemory(d, img, mem, 0));

	// ImageView
	// Only create if usageFlags is compatible
	if (
			(usageFlags &  VK_IMAGE_USAGE_SAMPLED_BIT) or
			(usageFlags &  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) or
			(usageFlags &  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) or
			(usageFlags &  VK_IMAGE_USAGE_STORAGE_BIT)) {
		VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.image = img;
		viewInfo.format = format;
		// viewInfo.format = format == vk::Format::eR8Uint ? vk::Format::eR8G8B8A8Uint : format;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.aspectMask = aspect;

		// view = std::move(d.createImageView(viewInfo));
		assertCallVk(vkCreateImageView(theDevice, &viewInfo, nullptr, &view));
	}
}

void ExBuffer::create(Device& device) {
	assert(givenSize>0);

	uint32_t idx = 0;


	VkBufferCreateInfo binfo {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
		{}, givenSize,
		usageFlags,
		// sharingMode, spec.queueFamilyIndices
		sharingMode, 0, nullptr
	};
	// buffer = std::move(vk::raii::Buffer{spec.dev.createBuffer(binfo)});
	assertCallVk(vkCreateBuffer(device, &binfo, nullptr, &buf));

	// auto req = buffer.getMemoryRequirements();
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, buf, &req);
	capacity = req.size;
	//printf(" - allocating buffer to memory type idx %u, givenSize %lu, residentSize %lu\n", idx, givenSize, residentSize);

	uint32_t memMask = req.memoryTypeBits;
	idx = findMemoryTypeIndex(device.pdevice, memPropFlags, memMask);

	VkMemoryAllocateInfo allocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, capacity, idx };

	VkMemoryAllocateFlagsInfo memAllocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT };
	if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		allocInfo.pNext = &memAllocInfo;
	}

	assertCallVk(vkAllocateMemory(device, &allocInfo, nullptr, &mem));

	assertCallVk(vkBindBufferMemory(device, buf, mem, 0));
}

ExImage::~ExImage() {
	if (own and img) vkDestroyImage(device, img, nullptr);
	img = nullptr;
}


void Barriers::append(ExImage& img, VkImageLayout to) {

	if (img.prevLayout != to) {
		imgBarriers.push_back(
				VkImageMemoryBarrier {
				VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				nullptr,
				{},{},
				img.prevLayout, to,
				{}, {},
				img.img,
				VkImageSubresourceRange { img.aspect, 0, 1, 0, 1}
				});

		img.prevLayout = to;
	}
}






void Command::executeAndPresent(DeviceQueueSpec& qds, SwapChain& sc, FrameData& fd) {

	if (fd.swapchainImg.prevLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
		Barriers barrier;
		barrier.append(fd.swapchainImg, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		this->barriers(barrier);
	}

	fd.cmd.end();

	Submission submission { qds };
	submission.fence = fd.frameDoneFence;
	submission.signalSemas = { fd.renderCompleteSema };
	submission.waitStages = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT };
	submission.submit(&cmdBuf, 1);

	VkResult result;
	VkPresentInfoKHR presentInfo {
			VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			nullptr,
			1, &fd.renderCompleteSema, // wait sema
			1, &sc.displaySwapchain,
			&fd.scIndx, &result
	};

	assertCallVk(vkQueuePresentKHR(qds.q, &presentInfo));
	assertCallVk(result);
}







void PipelineBuilder::init(
		const VertexInputDescription& vertexDesc,
		VkPrimitiveTopology topo,
		VkShaderModule vs, VkShaderModule fs) {
	{
		VkPipelineShaderStageCreateInfo pss_ci { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };

		pss_ci.stage = VK_SHADER_STAGE_VERTEX_BIT;
		pss_ci.module = vs;
		pss_ci.pName = "main";
		shaderStages.push_back(pss_ci);

		pss_ci.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		pss_ci.module = fs;
		pss_ci.pName = "main";
		shaderStages.push_back(pss_ci);
	}

	{
		VkPipelineVertexInputStateCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };
		info.vertexBindingDescriptionCount = vertexDesc.bindings.size();
		info.pVertexBindingDescriptions = vertexDesc.bindings.data();
		info.vertexAttributeDescriptionCount = vertexDesc.attributes.size();
		info.pVertexAttributeDescriptions = vertexDesc.attributes.data();
		vertexInputInfo = info;
	}

	{
		VkPipelineInputAssemblyStateCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
		
		info.topology = topo;
		info.primitiveRestartEnable = VK_FALSE;
		inputAssembly = info;
	}

	{
		VkPipelineRasterizationStateCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr };
		info.depthClampEnable = VK_FALSE;
		info.rasterizerDiscardEnable = VK_FALSE; //discards all primitives before the rasterization stage if enabled which we don't want

		auto polygonMode = VK_POLYGON_MODE_FILL;

		info.polygonMode = polygonMode;
		info.lineWidth = 1.0f;
		//info.cullMode = vk::CullModeFlagBits::eNone; //no backface cull
		info.cullMode = VK_CULL_MODE_BACK_BIT;
		info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		info.depthBiasEnable = VK_FALSE; //no depth bias
		info.depthBiasConstantFactor = 0.0f;
		info.depthBiasClamp = 0.0f;
		info.depthBiasSlopeFactor = 0.0f;

		rasterizer = info;
	}

	{
		VkPipelineDepthStencilStateCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr };

		info.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
		info.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
		info.depthCompareOp = depthTest ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS;
		info.depthBoundsTestEnable = VK_FALSE;
		info.minDepthBounds = 0.0f;
		info.maxDepthBounds = 1.0f;
		info.stencilTestEnable = VK_FALSE;
		depthState = info;
	}

	{
		VkPipelineMultisampleStateCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };

		info.sampleShadingEnable = VK_FALSE;
		info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		info.minSampleShading = 1.0f;
		info.pSampleMask = nullptr;
		info.alphaToCoverageEnable = VK_FALSE;
		info.alphaToOneEnable = VK_FALSE;
		multisampling = info;
	}

	if (not additiveBlending) {
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT ;
		//colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		this->colorBlendAttachment = colorBlendAttachment;
	} else if (replaceBlending) {
		// TODO
	} else {
		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT ;
		//colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		this->colorBlendAttachment = colorBlendAttachment;
	}
}

void GraphicsPipeline::create(float viewportXYWH[4], PipelineBuilder& builder, VkRenderPass pass, int subpass) {

	VkGraphicsPipelineCreateInfo ci { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };

	VkPipelineViewportStateCreateInfo viewportInfo;

	VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };
	colorBlending.logicOpEnable = VK_FALSE;
	//colorBlending.logicOpEnable = VK_TRUE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &builder.colorBlendAttachment;

	// Create layout
	{
		VkPipelineLayoutCreateInfo info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
		info.flags = {};
		info.pushConstantRangeCount = pushConstants.size();
		info.pPushConstantRanges = pushConstants.data();
		info.setLayoutCount = setLayouts.size();
		info.pSetLayouts = setLayouts.data();

		// pipelineLayout = std::move(device.createPipelineLayout(info));
		assertCallVk(vkCreatePipelineLayout(device, &info, nullptr, &layout));
	}


	ci.flags = builder.flags;
	ci.stageCount = builder.shaderStages.size();
	ci.pStages = builder.shaderStages.data();
	ci.pVertexInputState = &builder.vertexInputInfo;
	ci.pInputAssemblyState = &builder.inputAssembly;
	ci.pTessellationState = nullptr;
	ci.pViewportState = &viewportInfo;
	ci.pRasterizationState = &builder.rasterizer;
	ci.pMultisampleState = &builder.multisampling;
	ci.pDepthStencilState = &builder.depthState;
	ci.pColorBlendState = &colorBlending;
	ci.pDynamicState = nullptr;
	ci.layout = layout;
	ci.renderPass = pass;
	ci.subpass = 0;
	ci.basePipelineHandle = nullptr;
	ci.basePipelineIndex = 0;

	assertCallVk(vkCreateGraphicsPipelines(device, nullptr, 1, &ci, nullptr, &pipeline));
}
