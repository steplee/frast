#include "app.h"
#include "load_shader.hpp"

// #include <vulkan/vulkan_xcb.h>

#include <iostream>
#include <fstream>
#include <chrono>

#include <fmt/core.h>

#include <cstdlib>



namespace {

std::chrono::time_point<std::chrono::high_resolution_clock> __tp0;
float getSeconds(bool first=false) {
	if (first) {
		__tp0 = std::chrono::high_resolution_clock::now();
	}
	auto tp = std::chrono::high_resolution_clock::now();
	return static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(tp - __tp0).count()) * .000001f;
}
}

uint32_t findMemoryTypeIndex(const vk::PhysicalDevice& pdev, const vk::MemoryPropertyFlags& flags, uint32_t maskOrZero) {
	vk::PhysicalDeviceMemoryProperties props { pdev.getMemoryProperties() };

	for (int i=0; i<props.memoryTypeCount; i++) {
		if ( ((maskOrZero == 0) or ((1<<i) & maskOrZero)) )
			if ((props.memoryTypes[i].propertyFlags & flags) == flags) return i;
			// if ((props.memoryTypes[i].propertyFlags & flags)) return i;
	}
	throw std::runtime_error("failed to find memory type index.");
	return 99999999;
}


uint32_t AbstractSwapchain::acquireNextImage(vk::raii::Device& device, vk::Semaphore acquireSema, vk::Fence readyFence) {
	if (!headlessImages.size()) {
		uint32_t idx = sc.acquireNextImage(0, acquireSema, readyFence).second;
		return idx;
	} else {
		// device.signalSemaphore(vk::SemaphoreSignalInfo{acquireSema});
		device.resetFences({readyFence});
		return curIdx++ % headlessImages.size();
	}
}

void AbstractSwapchain::clear() {
	sc = nullptr;
	headlessImages.clear();
	headlessImages.clear();
	headlessMemory.clear();
	headlessCopyCmds.clear();
	headlessCopyDoneSemas.clear();
	headlessCopyDoneFences.clear();
}

/* ===================================================
 *
 *
 *                  BaseVkApp
 *
 *
 * =================================================== */

bool BaseVkApp::make_instance() {

	uint32_t desiredApiVersion = VK_HEADER_VERSION_COMPLETE;

	if (rti.enablePipeline) {
		// If using raytracing, we need 1.2+
		desiredApiVersion = VK_MAKE_VERSION(1,3,0);
	}

	vk::ApplicationInfo       applicationInfo{
			"vulkanApp",
			VK_MAKE_VERSION(1, 0, 0),
			"vulkanAppEngine",
			VK_MAKE_VERSION(1, 0, 0),
			// VK_MAKE_VERSION(1, 2, 0) };
			// VK_MAKE_VERSION(2, 0, 3) };
			// VK_HEADER_VERSION_COMPLETE
			// VK_MAKE_VERSION(1, 2, 0)
			desiredApiVersion
			};

	std::vector<char*> layers_ = {
#ifdef VULKAN_DEBUG
		//"VK_LAYER_LUNARG_parameter_validation",
      	//"VK_LAYER_LUNARG_device_limits", 
      	//"VK_LAYER_LUNARG_object_tracker",
      	//"VK_LAYER_LUNARG_image",
      	//"VK_LAYER_LUNARG_core_validation",
#endif
	};
	std::vector<char*>  extensions = {
		// VK_KHR_SURFACE_EXTENSION_NAME
		//,VK_KHR_XCB_SURFACE_EXTENSION_NAME
		//,VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME 
#ifdef VULKAN_DEBUG
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		,VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
	};

#ifdef VULKAN_DEBUG
	if (getenv("NO_VULKAN_DEBUG") == 0) {
		layers_.push_back( "VK_LAYER_KHRONOS_validation");
	}
#endif

	std::vector<std::string> extraExtensions = getWindowExtensions();
	fmt::print(" - Extra window exensions: "); for (auto s : extraExtensions) fmt::print("{}, ", s); fmt::print("\n");
	for (auto &extraExtension : extraExtensions) extensions.push_back((char*)extraExtension.c_str());



	fmt::print(" - Final Instance Extensions: "); for (auto s : extensions) fmt::print("{}, ", s); fmt::print("\n");

	vk::InstanceCreateInfo info {
			{},
			&applicationInfo,
			layers_,
			extensions
	};

	std::cout << " - Using layers:\n";
	for (auto l : layers_) std::cout << l << "\n";

	instance = std::move(vk::raii::Instance(ctx, info));
	return false;
}

bool BaseVkApp::make_gpu_device() {
	uint32_t ndevices = 8;
	VkPhysicalDevice pdevices[8];
	vkEnumeratePhysicalDevices(*instance, &ndevices, pdevices);

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


				myDeviceIdx = i;
				break;
			}
		}
	if (myDeviceIdx >= 999) {
		throw std::runtime_error(" - No device could be found (did you enable features not supported?)");
	}
	VkPhysicalDevice pdevice = pdevices[myDeviceIdx];
	pdeviceGpu = vk::raii::PhysicalDevice { instance, pdevice };

	// Find gfx enabled queue
	uint32_t nqprops = 32;
	VkQueueFamilyProperties qprops[32];
	vkGetPhysicalDeviceQueueFamilyProperties(pdevice, &nqprops, qprops);
	uint32_t qfamilyIdx = 99999;
	for (int i=0; i<nqprops; i++) {
		if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			qfamilyIdx = i;
			break;
		}
	}
	assert(qfamilyIdx < 9999);

	VkPhysicalDeviceProperties pdeviceProps;
	vkGetPhysicalDeviceProperties(pdevice, &pdeviceProps);
	printf(" - Selected Device '%s', queueFamily %u.\n", pdeviceProps.deviceName, qfamilyIdx);
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
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME
		// ,VK_EXT_MULTI_DRAW_EXTENSION_NAME  
		//,VK_KHR_16BIT_STORAGE_EXTENSION_NAME
	};

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

	VkPhysicalDeviceVulkan11Features extraFeatures3 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
	};
	extraFeatures3.storageBuffer16BitAccess = true;
	extraFeatures3.uniformAndStorageBuffer16BitAccess = true;
	pushIt((S*)&extraFeatures3);

	VkPhysicalDeviceFeatures2 extraFeatures4 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	extraFeatures4.features.shaderInt16 = true;
	pushIt((S*)&extraFeatures4);

	VkPhysicalDeviceIndexTypeUint8FeaturesEXT extraFeatures5 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
		nullptr,
		true
	};
	pushIt((S*)&extraFeatures5);

	vk::PhysicalDeviceMultiDrawFeaturesEXT extraFeatures6;
	extraFeatures6.multiDraw = true;
	pushIt((S*)&extraFeatures6);

	vk::PhysicalDeviceRobustness2FeaturesEXT extraFeatures7;
    extraFeatures7.nullDescriptor = true;
	pushIt((S*)&extraFeatures7);

	vk::PhysicalDeviceVulkan12Features extraFeatures8;
	extraFeatures8.shaderInt8 = true;
	extraFeatures8.uniformBufferStandardLayout = true;
	extraFeatures8.uniformAndStorageBuffer8BitAccess = true;
	extraFeatures8.storageBuffer8BitAccess = true;
	extraFeatures8.bufferDeviceAddress = true;
	pushIt((S*)&extraFeatures8);


	// -----------------------------------------------
	//      RayTracing Features
	// -----------------------------------------------

	if (rti.enablePipeline) {
		pushIt((S*)&rti.accFeatures);
		pushIt((S*)&rti.rayPiplelineFeatures);
	}
	if (rti.enableQuery) {
		assert((not rti.enableQuery) && "rayQuery not supported now");
	}

	// -----------------------------------------------
	//      Ready to make device!
	// -----------------------------------------------

	fmt::print(" - Final Device Extensions: "); for (auto s : exts) fmt::print("{}, ", s); fmt::print("\n");

    VkDeviceCreateInfo dinfo {
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			_first,
			{},
			n_q_family, qinfos,
			0, nullptr,
			(uint32_t)exts.size(), exts.data(),
			nullptr };
	deviceGpu = std::move(vk::raii::Device(pdeviceGpu, dinfo, nullptr));

	uint32_t instanceVersion = ctx.enumerateInstanceVersion();
	// std::cout << " - pdevice api version: " << pdeviceProps.apiVersion << "\n";
	fmt::print(" - instance version {} :: {} {} {}\n", instanceVersion, VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion));
	fmt::print(" - pdevice api version {} :: {} {} {}\n", pdeviceProps.apiVersion, VK_API_VERSION_MAJOR(pdeviceProps.apiVersion), VK_API_VERSION_MINOR(pdeviceProps.apiVersion), VK_API_VERSION_PATCH(pdeviceProps.apiVersion));
	std::cout << " - device vk header version: " << deviceGpu.getDispatcher()->getVkHeaderVersion() << " header " << VK_HEADER_VERSION << "\n";

	// Get Queue
	queueGfx = deviceGpu.getQueue(qfamilyIdx, 0);



	if (rti.enablePipeline) {
		VkPhysicalDeviceProperties2 deviceProperties2{};
		deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		deviceProperties2.pNext = &rti.rayPipelineProps;
		vkGetPhysicalDeviceProperties2(*pdeviceGpu, &deviceProperties2);
	}

	return false;
}

bool BaseVkApp::make_headless_swapchain() {
	scSurfaceFormat.format = vk::Format::eR8G8B8A8Unorm;

	scNumImages = 2;

	vk::ImageCreateInfo imageInfo = { };
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = scSurfaceFormat.format;
    imageInfo.extent = vk::Extent3D { (uint32_t)windowWidth, (uint32_t)windowHeight, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst
		| vk::ImageUsageFlagBits::eTransferSrc
		// | vk::ImageUsageFlagBits::eSampled
		| vk::ImageUsageFlagBits::eColorAttachment
		// | vk::ImageUsageFlagBits::eDepthStencilAttachment
		| vk::ImageUsageFlagBits::eStorage;

	for (uint32_t i=0; i<scNumImages; i++) {

		sc.headlessImages.push_back(std::move(deviceGpu.createImage(imageInfo)));

		uint64_t size_ = sc.headlessImages.back().getMemoryRequirements().size;
		uint64_t minSize = std::max(size_, ((size_+0x1000-1)/0x1000)*0x1000);
		auto memPropFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
		auto idx = findMemoryTypeIndex(*pdeviceGpu, memPropFlags);
		vk::MemoryAllocateInfo allocInfo { std::max(minSize,size_), idx };
		sc.headlessMemory.push_back(std::move(vk::raii::DeviceMemory(deviceGpu, allocInfo)));
		// std::cout << "  headless image with size " << size_  << " v " << 4*windowWidth*windowHeight << "\n";

		sc.headlessImages.back().bindMemory(*sc.headlessMemory.back(), 0);

		vk::ImageViewCreateInfo viewInfo {
			{},
			*sc.headlessImages[i],
			vk::ImageViewType::e2D,
			vk::Format(scSurfaceFormat.format),
			{},
			vk::ImageSubresourceRange {
				vk::ImageAspectFlagBits::eColor,
				0, 1, 0, 1 }
		};
		scImageViews.push_back(std::move(deviceGpu.createImageView(viewInfo)));

	}

	vk::CommandPoolCreateInfo poolInfo {
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyGfxIdxs[0] };
	commandPool = std::move(deviceGpu.createCommandPool(poolInfo));

	// Allocate command buffers for copying.
	vk::CommandBufferAllocateInfo bufInfo {
		*commandPool,
		vk::CommandBufferLevel::ePrimary,
		(uint32_t)scNumImages };
	sc.headlessCopyCmds = std::move(deviceGpu.allocateCommandBuffers(bufInfo));

	for (int i=0; i<scNumImages; i++) {
		sc.headlessCopyDoneSemas.push_back(std::move(deviceGpu.createSemaphore({})));
		sc.headlessCopyDoneFences.push_back(std::move(deviceGpu.createFence({})));
	}

	return false;
}

bool BaseVkApp::make_surface() {
	/*
	vk::XcbSurfaceCreateInfoKHR s_ci = {
		{},
		xcbConn,
		xcbWindow
	};

	surface = std::move(vk::raii::SurfaceKHR{instance, s_ci});
	*/

	VkSurfaceKHR surface_;
	auto res = glfwCreateWindowSurface(*instance, glfwWindow, NULL, &surface_);
	fmt::print(" - glfwCreateWindowSurface res: {}\n", res);
	surface = std::move(vk::raii::SurfaceKHR{instance, surface_});
	// surface = std::move(vk::raii::SurfaceKHR{surface_});
	return false;
}

BaseVkApp::~BaseVkApp() {
	sc.clear();
	// vkDestroySurfaceKHR(surface);
}

bool BaseVkApp::make_swapchain() {
	/*
      VULKAN_HPP_NAMESPACE::SwapchainCreateFlagsKHR flags_         = {},
      VULKAN_HPP_NAMESPACE::SurfaceKHR              surface_       = {},
      uint32_t                                      minImageCount_ = {},
      VULKAN_HPP_NAMESPACE::Format                  imageFormat_   = VULKAN_HPP_NAMESPACE::Format::eUndefined,
      VULKAN_HPP_NAMESPACE::ColorSpaceKHR   imageColorSpace_  = VULKAN_HPP_NAMESPACE::ColorSpaceKHR::eSrgbNonlinear,
      VULKAN_HPP_NAMESPACE::Extent2D        imageExtent_      = {},
      uint32_t                              imageArrayLayers_ = {},
      VULKAN_HPP_NAMESPACE::ImageUsageFlags imageUsage_       = {},
      VULKAN_HPP_NAMESPACE::SharingMode     imageSharingMode_ = VULKAN_HPP_NAMESPACE::SharingMode::eExclusive,
      uint32_t                              queueFamilyIndexCount_ = {},
      const uint32_t *                      pQueueFamilyIndices_   = {},
      VULKAN_HPP_NAMESPACE::SurfaceTransformFlagBitsKHR preTransform_ =
        VULKAN_HPP_NAMESPACE::SurfaceTransformFlagBitsKHR::eIdentity,
      VULKAN_HPP_NAMESPACE::CompositeAlphaFlagBitsKHR compositeAlpha_ =
        VULKAN_HPP_NAMESPACE::CompositeAlphaFlagBitsKHR::eOpaque,
      VULKAN_HPP_NAMESPACE::PresentModeKHR presentMode_  = VULKAN_HPP_NAMESPACE::PresentModeKHR::eImmediate,
      VULKAN_HPP_NAMESPACE::Bool32         clipped_      = {},
      VULKAN_HPP_NAMESPACE::SwapchainKHR   oldSwapchain_ = {} ) VULKAN_HPP_NOEXCEPT
	  */

	VkSurfaceFormatKHR surfFormat;
	{
		uint32_t formats_count;
		if( (vkGetPhysicalDeviceSurfaceFormatsKHR( *pdeviceGpu, *surface, &formats_count, nullptr ) != VK_SUCCESS) ||
				(formats_count == 0) )
			throw std::runtime_error("failed to enumerate surface formats (or # = 0)");
		std::vector<VkSurfaceFormatKHR> surface_formats( formats_count );
		if( vkGetPhysicalDeviceSurfaceFormatsKHR( *pdeviceGpu, *surface, &formats_count, &surface_formats[0] ) != VK_SUCCESS )
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
	scSurfaceFormat = vk::SurfaceFormatKHR { surfFormat };

	// See https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VkPresentModeKHR.html
	// presentMode = vk::PresentModeKHR::eFifo;
	// presentMode = vk::PresentModeKHR::eImmediate;
	presentMode = vk::PresentModeKHR::eFifoRelaxed;

	vk::SwapchainCreateInfoKHR sc_ci_ {
		{},
		*surface,
		3,
		scSurfaceFormat.format,                        // VkFormat                       imageFormat
		scSurfaceFormat.colorSpace,                    // VkColorSpaceKHR                imageColorSpace
		vk::Extent2D{windowWidth,windowHeight},
		1,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		vk::SharingMode::eExclusive,                    // VkSharingMode                  imageSharingMode
		0,
		nullptr,
		vk::SurfaceTransformFlagBitsKHR::eIdentity,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		presentMode,
		VK_TRUE, nullptr
	};
	scNumImages = 3;

	sc.sc = std::move(deviceGpu.createSwapchainKHR(sc_ci_));

	for (int i=0; i<scNumImages; i++) {
		vk::ImageViewCreateInfo viewInfo {
			{},
			getSwapChainImage(i),
			vk::ImageViewType::e2D,
			vk::Format(scSurfaceFormat.format),
			{},
			vk::ImageSubresourceRange {
				vk::ImageAspectFlagBits::eColor,
				0, 1, 0, 1 }
		};
		scImageViews.push_back(std::move(deviceGpu.createImageView(viewInfo)));
	}

	vk::CommandPoolCreateInfo poolInfo {
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyGfxIdxs[0] };
	commandPool = std::move(deviceGpu.createCommandPool(poolInfo));

	return false;
}
bool BaseVkApp::make_frames() {

	frameOverlap = scNumImages;
	// frameOverlap = 1;

	vk::CommandBufferAllocateInfo bufInfo {
		*commandPool,
		vk::CommandBufferLevel::ePrimary,
		(uint32_t)scNumImages };
	std::vector<vk::raii::CommandBuffer> commandBuffers = std::move(deviceGpu.allocateCommandBuffers(bufInfo));

	for (int i=0; i<frameOverlap; i++) {
		frameDatas.push_back(FrameData{});
		frameDatas.back().scAcquireSema = std::move(deviceGpu.createSemaphore({}));
		frameDatas.back().renderCompleteSema = std::move(deviceGpu.createSemaphore({}));
		frameDatas.back().frameReadyFence = std::move(deviceGpu.createFence({}));
		frameDatas.back().frameDoneFence = std::move(deviceGpu.createFence({vk::FenceCreateFlagBits::eSignaled}));
		//frameDatas.back().frameReadyFence = std::move(deviceGpu.createFence({}));
		//frameDatas.back().frameDoneFence = std::move(deviceGpu.createFence({}));
		frameDatas.back().cmd = std::move(commandBuffers[i]);
		// frameDatas.back().scIndx = i; // This is actually done in acquireFrame()!
	}

	// If headless we should jumpstart the frame semaphores
	if (headless)
		for (int i=0; i<frameOverlap; i++) {
			deviceGpu.resetFences({*frameDatas[i].frameDoneFence});
			frameDatas[i].cmd.reset();
			frameDatas[i].cmd.begin({});
			frameDatas[i].cmd.end();
			vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;
			vk::SubmitInfo submitInfo {
				0,0,
					&waitMask,
					(uint32_t)1, &*frameDatas[i].cmd,
					1, &*frameDatas[i].scAcquireSema // signal sema
			};
			// fmt::print(" - submit render wait on {}\n", (void*)&*frameDatas[i].scAcquireSema);
			queueGfx.submit(submitInfo, *frameDatas[i].frameDoneFence);
			deviceGpu.waitForFences({*frameDatas[i].frameDoneFence}, true, 999999999);
			deviceGpu.resetFences({*frameDatas[i].frameDoneFence});
		}

	return false;
}

bool BaseVkApp::make_basic_render_stuff() {
	// Create renderpass and framebuffers

	/*

    VULKAN_HPP_CONSTEXPR AttachmentDescription(
      VULKAN_HPP_NAMESPACE::AttachmentDescriptionFlags flags_         = {},
      VULKAN_HPP_NAMESPACE::Format                     format_        = VULKAN_HPP_NAMESPACE::Format::eUndefined,
      VULKAN_HPP_NAMESPACE::SampleCountFlagBits        samples_       = VULKAN_HPP_NAMESPACE::SampleCountFlagBits::e1,
      VULKAN_HPP_NAMESPACE::AttachmentLoadOp           loadOp_        = VULKAN_HPP_NAMESPACE::AttachmentLoadOp::eLoad,
      VULKAN_HPP_NAMESPACE::AttachmentStoreOp          storeOp_       = VULKAN_HPP_NAMESPACE::AttachmentStoreOp::eStore,
      VULKAN_HPP_NAMESPACE::AttachmentLoadOp           stencilLoadOp_ = VULKAN_HPP_NAMESPACE::AttachmentLoadOp::eLoad,
      VULKAN_HPP_NAMESPACE::AttachmentStoreOp stencilStoreOp_         = VULKAN_HPP_NAMESPACE::AttachmentStoreOp::eStore,
      VULKAN_HPP_NAMESPACE::ImageLayout       initialLayout_          = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined,
      VULKAN_HPP_NAMESPACE::ImageLayout       finalLayout_            = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined )

    VULKAN_HPP_CONSTEXPR SubpassDescription(
      VULKAN_HPP_NAMESPACE::SubpassDescriptionFlags flags_       = {},
      VULKAN_HPP_NAMESPACE::PipelineBindPoint pipelineBindPoint_ = VULKAN_HPP_NAMESPACE::PipelineBindPoint::eGraphics,
      uint32_t                                inputAttachmentCount_              = {},
      const VULKAN_HPP_NAMESPACE::AttachmentReference * pInputAttachments_       = {},
      uint32_t                                          colorAttachmentCount_    = {},
      const VULKAN_HPP_NAMESPACE::AttachmentReference * pColorAttachments_       = {},
      const VULKAN_HPP_NAMESPACE::AttachmentReference * pResolveAttachments_     = {},
      const VULKAN_HPP_NAMESPACE::AttachmentReference * pDepthStencilAttachment_ = {},
      uint32_t                                          preserveAttachmentCount_ = {},
      const uint32_t *                                  pPreserveAttachments_    = {} ) VULKAN_HPP_NOEXCEPT

    SubpassDescription(
      VULKAN_HPP_NAMESPACE::SubpassDescriptionFlags flags_,
      VULKAN_HPP_NAMESPACE::PipelineBindPoint       pipelineBindPoint_,
      VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const VULKAN_HPP_NAMESPACE::AttachmentReference> const &
        inputAttachments_,
      VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const VULKAN_HPP_NAMESPACE::AttachmentReference> const &
        colorAttachments_ = {},
      VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const VULKAN_HPP_NAMESPACE::AttachmentReference> const &
                                                                            resolveAttachments_      = {},
      const VULKAN_HPP_NAMESPACE::AttachmentReference *                     pDepthStencilAttachment_ = {},
      VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const uint32_t> const & preserveAttachments_     = {} )
							*/

	// Color
	vk::AttachmentDescription colorAttachment {
			{},
			scSurfaceFormat.format,
			vk::SampleCountFlagBits::e1,
			//vk::AttachmentLoadOp::eStore, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal
			// vk::ImageLayout::ePresentSrcKHR
	};

	vk::AttachmentReference colorAttachmentRef {
		0,
		vk::ImageLayout::eColorAttachmentOptimal
	};

	// Depth
	for (int i=0; i<scNumImages; i++) {
		ResidentImage dimg;
		dimg.createAsDepthBuffer(uploader, windowHeight, windowWidth);
		//dimg.create(deviceGpu, *pdeviceGpu);
		simpleRenderPass.depthImages.push_back(std::move(dimg));
	}
	vk::AttachmentDescription depthAttachment {
			{},
			vk::Format { simpleRenderPass.depthImages[0].format },
			vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eUndefined,
			//vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eDepthStencilAttachmentOptimal };

	vk::AttachmentReference depthAttachmentRef {
		1,
		vk::ImageLayout::eDepthStencilAttachmentOptimal
	};

	std::vector<vk::SubpassDescription> subpasses {
		{ {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef },
		// { {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef }
	};


	// Depth dependency
      /*SubpassDependency( uint32_t                                 srcSubpass_      = {},
                         uint32_t                                 dstSubpass_      = {},
                         VULKAN_HPP_NAMESPACE::PipelineStageFlags srcStageMask_    = {},
                         VULKAN_HPP_NAMESPACE::PipelineStageFlags dstStageMask_    = {},
                         VULKAN_HPP_NAMESPACE::AccessFlags        srcAccessMask_   = {},
                         VULKAN_HPP_NAMESPACE::AccessFlags        dstAccessMask_   = {},
                         VULKAN_HPP_NAMESPACE::DependencyFlags    dependencyFlags_ = {} ) VULKAN_HPP_NOEXCEPT*/
	std::vector<vk::SubpassDependency> dependencies = {
		// Depth
		{
			VK_SUBPASS_EXTERNAL, 0,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			{},
			vk::AccessFlagBits::eDepthStencilAttachmentWrite,
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


	vk::AttachmentDescription atts[2] = { colorAttachment, depthAttachment };
	vk::RenderPassCreateInfo rpInfo {
		{},
		{ 2, atts },
		// { 1, &subpass0 },
		subpasses,
		// { 1,dependencies }
		{ dependencies }
	};
	simpleRenderPass.pass = std::move(deviceGpu.createRenderPass(rpInfo));


    /*VULKAN_HPP_CONSTEXPR FramebufferCreateInfo( VULKAN_HPP_NAMESPACE::FramebufferCreateFlags flags_           = {},
                                                VULKAN_HPP_NAMESPACE::RenderPass             renderPass_      = {},
                                                uint32_t                                     attachmentCount_ = {},
                                                const VULKAN_HPP_NAMESPACE::ImageView *      pAttachments_    = {},
                                                uint32_t                                     width_           = {},
                                                uint32_t                                     height_          = {},
                                                uint32_t layers_ = {} ) VULKAN_HPP_NOEXCEPT*/
	for (int i=0; i<scNumImages; i++) {

		vk::ImageView views[2] = { getSwapChainImageView(i), *simpleRenderPass.depthImages[i].view };

		vk::FramebufferCreateInfo fbInfo {
			{},
			*simpleRenderPass.pass,
			//vk::ArrayProxyNoTemporaries<const vk::ImageView>{ getSwapChainImageView(i) },
			//1, &getSwapChainImageView(i),
			{ 2, views},
			windowWidth, windowHeight,
			1
		};
		simpleRenderPass.framebuffers.push_back(std::move(deviceGpu.createFramebuffer(fbInfo)));
	}


	printf(" - Created %zu framebuffers.\n", simpleRenderPass.framebuffers.size());

	return false;
}

BaseVkApp::BaseVkApp() {
	windowWidth = 640;
	windowHeight = 480;
}

void BaseVkApp::initVk() {
	std::cout << " - initial      : " << *deviceGpu << " " << *surface << "\n";

	make_instance();


	if (headless) {
		make_gpu_device();
		make_headless_swapchain();
		scSurfaceFormat = vk::SurfaceFormatKHR {};
		scSurfaceFormat.format = vk::Format::eR8G8B8A8Unorm;
		scSurfaceFormat.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
		uploader = std::move(Uploader(this, *queueGfx));
	} else {
		setupWindow();

		make_gpu_device();
		uploader = std::move(Uploader(this, *queueGfx));

		make_surface();
		make_swapchain();
	}



	make_frames();

	make_basic_render_stuff();
}

void BaseVkApp::executeCommandsThenPresent(std::vector<vk::CommandBuffer>& cmds, RenderState& rs) {
	auto& fd = *rs.frameData;
	vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;
	vk::Semaphore semas[1] = { *fd.scAcquireSema };
	uint32_t nsema = 1;
	if (headless and not fd.useAcquireSema) nsema = 0;
	// if (headless) nsema = 0;
	vk::SubmitInfo submitInfo {
		nsema, semas, // wait sema
			&waitMask,
			//1, &(*commandBuffers[fd.scIndx]),
			(uint32_t)cmds.size(), cmds.data(),
			1, &*fd.renderCompleteSema // signal sema
	};
	// fmt::print(" - submit render wait on {}\n", (void*)&*fd.scAcquireSema);
	queueGfx.submit(submitInfo, *fd.frameDoneFence);
	// TODO dsiable
	deviceGpu.waitForFences({*fd.frameDoneFence}, true, 999999999);

	if (headless) {
		// Copy output and what not
		handleCompletedHeadlessRender(renderState, fd);
	} else {
		// handleCompletedHeadlessRender(renderState, fd);
		vk::PresentInfoKHR presentInfo {
			1, &*fd.renderCompleteSema, // wait sema
				1, &(*sc.sc),
				&fd.scIndx, nullptr
		};
		queueGfx.presentKHR(presentInfo);
	}
}

bool BaseVkApp::getDepthImage(ResidentImage& out, const FrameData& fd, const RenderState& rs) {

}

/* ===================================================
 *
 *
 *          PipelineBuilder & PipelineStuff
 *
 *
 * =================================================== */

void PipelineBuilder::init(
	const VertexInputDescription& vertexDesc,
	vk::PrimitiveTopology topo,
	vk::ShaderModule vs, vk::ShaderModule fs) {

	{
		vk::PipelineShaderStageCreateInfo pss_ci{};

		pss_ci.stage = vk::ShaderStageFlagBits::eVertex;
		pss_ci.module = vs;
		pss_ci.pName = "main";
		shaderStages.push_back(pss_ci);

		pss_ci.stage = vk::ShaderStageFlagBits::eFragment;
		pss_ci.module = fs;
		pss_ci.pName = "main";
		shaderStages.push_back(pss_ci);
	}

	{
		vk::PipelineVertexInputStateCreateInfo info = {};
		//no vertex bindings or attributes
		info.vertexBindingDescriptionCount = vertexDesc.bindings.size();
		info.pVertexBindingDescriptions = vertexDesc.bindings.data();
		info.vertexAttributeDescriptionCount = vertexDesc.attributes.size();
		info.pVertexAttributeDescriptions = vertexDesc.attributes.data();
		vertexInputInfo = info;
	}

	{
		vk::PipelineInputAssemblyStateCreateInfo info = {};
		
		//auto topo = vk::PrimitiveTopology::eTriangleList;
		info.topology = topo;
		info.primitiveRestartEnable = VK_FALSE;
		inputAssembly = info;
	}

	{
		vk::PipelineRasterizationStateCreateInfo info = {};
		info.depthClampEnable = VK_FALSE;
		info.rasterizerDiscardEnable = VK_FALSE; //discards all primitives before the rasterization stage if enabled which we don't want

		auto polygonMode = vk::PolygonMode::eFill;

		info.polygonMode = polygonMode;
		info.lineWidth = 1.0f;
		//info.cullMode = vk::CullModeFlagBits::eNone; //no backface cull
		info.cullMode = vk::CullModeFlagBits::eBack;
		info.frontFace = vk::FrontFace::eCounterClockwise;
		info.depthBiasEnable = VK_FALSE; //no depth bias
		info.depthBiasConstantFactor = 0.0f;
		info.depthBiasClamp = 0.0f;
		info.depthBiasSlopeFactor = 0.0f;

		rasterizer = info;
	}

	{
		vk::PipelineDepthStencilStateCreateInfo info = {};

		info.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
		info.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
		info.depthCompareOp = depthTest ? vk::CompareOp::eLess : vk::CompareOp::eAlways;
		info.depthBoundsTestEnable = VK_FALSE;
		info.minDepthBounds = 0.0f;
		info.maxDepthBounds = 1.0f;
		info.stencilTestEnable = VK_FALSE;
		depthState = info;
	}

	{
		vk::PipelineMultisampleStateCreateInfo info = {};

		info.sampleShadingEnable = VK_FALSE;
		info.rasterizationSamples = vk::SampleCountFlagBits::e1; // disabled
		info.minSampleShading = 1.0f;
		info.pSampleMask = nullptr;
		info.alphaToCoverageEnable = VK_FALSE;
		info.alphaToOneEnable = VK_FALSE;
		multisampling = info;
	}

	if (not additiveBlending) {
		vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA ;
		//colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
		this->colorBlendAttachment = colorBlendAttachment;
	} else if (replaceBlending) {
	} else {
		vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
			vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB |
			vk::ColorComponentFlagBits::eA ;
		//colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
		colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
		this->colorBlendAttachment = colorBlendAttachment;
	}

}


bool PipelineStuff::setup_viewport(float w, float h, float x, float y) {
	viewport = vk::Viewport {
		x, y,
		w, h,
		0, 1
	};
	scissor = vk::Rect2D {
		{ 0, 0 },
		{ (uint32_t)(w+.1f), (uint32_t)(h+.1f) }
	};
	return false;
}

bool PipelineStuff::build(PipelineBuilder& builder, vk::raii::Device& device, const vk::RenderPass& pass, uint32_t subpass) {
	vk::PipelineViewportStateCreateInfo viewportState = {};
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	//setup dummy color blending. We aren't using transparent objects yet
	//the blending is just "no blend", but we do write to the color attachment
	vk::PipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.logicOpEnable = VK_FALSE;
	//colorBlending.logicOpEnable = VK_TRUE;
	colorBlending.logicOp = vk::LogicOp::eCopy;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &builder.colorBlendAttachment;

	// Create layout
	{
		vk::PipelineLayoutCreateInfo info{};
		info.flags = {};
		info.pushConstantRangeCount = pushConstants.size();
		info.pPushConstantRanges = pushConstants.data();
		info.setLayoutCount = setLayouts.size();
		info.pSetLayouts = setLayouts.data();

		pipelineLayout = std::move(device.createPipelineLayout(info));
	}

	/*
    VULKAN_HPP_CONSTEXPR_14 GraphicsPipelineCreateInfo(
      VULKAN_HPP_NAMESPACE::PipelineCreateFlags                          flags_               = {},
      uint32_t                                                           stageCount_          = {},
      const VULKAN_HPP_NAMESPACE::PipelineShaderStageCreateInfo *        pStages_             = {},
      const VULKAN_HPP_NAMESPACE::PipelineVertexInputStateCreateInfo *   pVertexInputState_   = {},
      const VULKAN_HPP_NAMESPACE::PipelineInputAssemblyStateCreateInfo * pInputAssemblyState_ = {},
      const VULKAN_HPP_NAMESPACE::PipelineTessellationStateCreateInfo *  pTessellationState_  = {},
      const VULKAN_HPP_NAMESPACE::PipelineViewportStateCreateInfo *      pViewportState_      = {},
      const VULKAN_HPP_NAMESPACE::PipelineRasterizationStateCreateInfo * pRasterizationState_ = {},
      const VULKAN_HPP_NAMESPACE::PipelineMultisampleStateCreateInfo *   pMultisampleState_   = {},
      const VULKAN_HPP_NAMESPACE::PipelineDepthStencilStateCreateInfo *  pDepthStencilState_  = {},
      const VULKAN_HPP_NAMESPACE::PipelineColorBlendStateCreateInfo *    pColorBlendState_    = {},
      const VULKAN_HPP_NAMESPACE::PipelineDynamicStateCreateInfo *       pDynamicState_       = {},
      VULKAN_HPP_NAMESPACE::PipelineLayout                               layout_              = {},
      VULKAN_HPP_NAMESPACE::RenderPass                                   renderPass_          = {},
      uint32_t                                                           subpass_             = {},
      VULKAN_HPP_NAMESPACE::Pipeline                                     basePipelineHandle_  = {},
      int32_t                                                            basePipelineIndex_   = {} ) VULKAN_HPP_NOEXCEPT
	  */

	vk::GraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.stageCount = builder.shaderStages.size();
	pipelineInfo.pStages = builder.shaderStages.data();
	pipelineInfo.pVertexInputState = &builder.vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &builder.inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &builder.rasterizer;
	pipelineInfo.pMultisampleState = &builder.multisampling;
	pipelineInfo.pDepthStencilState = &builder.depthState;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = *pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = subpass;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	pipeline = std::move(device.createGraphicsPipeline(nullptr, pipelineInfo));

	return false;
}


bool RaytracePipelineStuff::setup_viewport(float w, float h, float x, float y) {
	viewport = vk::Viewport { x, y, w, h, 0, 1 };
	scissor = vk::Rect2D { { 0, 0 }, { (uint32_t)(w+.1f), (uint32_t)(h+.1f) } };
	return false;
}
bool RaytracePipelineStuff::build(BaseVkApp* app) {
	vk::raii::Device& device = app->deviceGpu;
	vk::raii::PhysicalDevice& pd = app->pdeviceGpu;

	// storageImage.createAsStorage(device, pd, viewport.height, viewport.width, vk::Format::eR32G32B32A32Sfloat);
	storageImage.createAsStorage(device, pd, viewport.height, viewport.width, app->scSurfaceFormat.format);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
	std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;

	// Create shader stages & groups
	{
		if (((VkShaderModule)*gen) != 0) {
			vk::PipelineShaderStageCreateInfo ci{};
			ci.stage = vk::ShaderStageFlagBits::eRaygenKHR;
			ci.module = *gen;
			ci.pName = "main";
			shaderStages.push_back(ci);

			vk::RayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		if (((VkShaderModule)*closestHit) != 0) {
			vk::PipelineShaderStageCreateInfo ci{};
			ci.stage = vk::ShaderStageFlagBits::eClosestHitKHR;
			ci.module = *closestHit;
			ci.pName = "main";
			shaderStages.push_back(ci);

			vk::RayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
			shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		if (((VkShaderModule)*miss) != 0) {
			vk::PipelineShaderStageCreateInfo ci{};
			ci.stage = vk::ShaderStageFlagBits::eMissKHR;
			ci.module = *miss;
			ci.pName = "main";
			shaderStages.push_back(ci);

			vk::RayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}
	}

	// Create layout
	{
		vk::PipelineLayoutCreateInfo info{};
		info.flags = {};
		info.pushConstantRangeCount = pushConstants.size();
		info.pPushConstantRanges = pushConstants.data();
		info.setLayoutCount = setLayouts.size();
		info.pSetLayouts = setLayouts.data();

		pipelineLayout = std::move(device.createPipelineLayout(info));
	}

	vk::RayTracingPipelineCreateInfoKHR info;
	info.stageCount = (uint32_t) shaderStages.size();
	info.pStages =  shaderStages.data();
	info.groupCount =  (uint32_t)shaderGroups.size();
	info.pGroups =  shaderGroups.data();
	info.maxPipelineRayRecursionDepth = 12;
	info.layout = *pipelineLayout;

	pipeline = std::move(device.createRayTracingPipelinesKHR(nullptr, nullptr, info)[0]);

	// Create SBT
	{
		uint32_t handleSize = app->rti.rayPipelineProps.shaderGroupHandleSize;
		uint32_t a = app->rti.rayPipelineProps.shaderGroupHandleAlignment;
		handleSizeAligned = ((handleSize+a-1) / a) * a;
		uint32_t groups = (uint32_t)shaderGroups.size();
		uint32_t sbtSize = groups * handleSizeAligned;
		fmt::print(" - handle size {}, alignment {}, chosen {}\n", handleSize, a, handleSizeAligned);

		std::vector<uint8_t> shaderHandleStorage = pipeline.getRayTracingShaderGroupHandlesKHR<uint8_t>(0, groups, sbtSize);

		genSBT.setAsBuffer(handleSize, true, vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
		chitSBT.setAsBuffer(handleSize, true, vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
		missSBT.setAsBuffer(handleSize, true, vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
		genSBT.memPropFlags |= vk::MemoryPropertyFlagBits::eHostCoherent;
		chitSBT.memPropFlags |= vk::MemoryPropertyFlagBits::eHostCoherent;
		missSBT.memPropFlags |= vk::MemoryPropertyFlagBits::eHostCoherent;

		genSBT.create(device, *pd, app->queueFamilyGfxIdxs);
		missSBT.create(device, *pd, app->queueFamilyGfxIdxs);
		chitSBT.create(device, *pd, app->queueFamilyGfxIdxs);


		// Copy handles
		genSBT.map();
		missSBT.map();
		chitSBT.map();
		memcpy(genSBT.mapped, shaderHandleStorage.data(), handleSize);
		memcpy(chitSBT.mapped, shaderHandleStorage.data() + handleSizeAligned, handleSize);
		memcpy(missSBT.mapped, shaderHandleStorage.data() + handleSizeAligned * 2, handleSize);
	}


	return false;
}



float BaseVkApp::time() {
	return getSeconds() - time0;
}

bool BaseVkApp::isDone() {
	return isDone_;
}

FrameData& BaseVkApp::acquireFrame() {
	int ii1 = renders == 0 ? frameOverlap-1 : (renders-1) % frameOverlap;
	int ii = renders % frameOverlap;

	FrameData& fd = frameDatas[ii];

	deviceGpu.waitForFences({*frameDatas[ii].frameDoneFence}, true, 999999999);
	deviceGpu.resetFences({*frameDatas[ii].frameDoneFence});

	// if (headless) {
	// } else {
	// }
	// uint32_t new_scIndx = sc.acquireNextImage(0, *fd.scAcquireSema, *fd.frameReadyFence).second;

	// TODO Stopped here, pickup tomorrow
	uint32_t new_scIndx;
	if (headless) {
		// new_scIndx = sc.acquireNextImage(deviceGpu, *fd.scAcquireSema );
		new_scIndx = sc.acquireNextImage(deviceGpu, *fd.scAcquireSema, *fd.frameReadyFence);
		// deviceGpu.waitForFences({*fd.frameReadyFence}, true, 999999999);
		deviceGpu.resetFences({*fd.frameReadyFence});

		// deviceGpu.signalSemaphore(vk::SemaphoreSignalInfo{*fd.scAcquireSema});
		// VkSemaphoreSignalInfo ssinfo;
		// ssinfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
		// ssinfo.semaphore = *fd.scAcquireSema;
		// vkSignalSemaphore(*deviceGpu, &ssinfo);

	} else {
		new_scIndx = sc.acquireNextImage(deviceGpu, *fd.scAcquireSema, *fd.frameReadyFence);
		deviceGpu.waitForFences({*fd.frameReadyFence}, true, 999999999);
		deviceGpu.resetFences({*fd.frameReadyFence});
	}

	fd.scIndx = new_scIndx;


	renders++;
	if (renders == 1) {
		time0 = getSeconds(true);
		for (auto& fd : frameDatas) fd.time = time0;
	}
	auto time_ = time();
	fd.dt = time_ - frameDatas[ii1].time;
	fd.time = time_;
	fd.n = renders-1;
	// std::cout << " - acquired idx " << fd.scIndx << " for frame " << fd.n << " at i " << ii << "\n";

	if (renders > 1)
		fpsMeter = fpsMeter * .95 + .05 * (1. / fd.dt);
	if (renders % (60*10) == 0)
		printf(" - acquireFrame() (sc indx %u) (frame %d) (t %f, fps %f, dt %f)\n", ii, fd.n, fd.time, fpsMeter, fd.dt);

	return fd;
}


/* ===================================================
 *
 *
 *                  VkApp
 *
 *
 * =================================================== */

VkApp::~VkApp() {
	// Must wait before raii destructors called.
	deviceGpu.waitIdle();
}

VkApp::VkApp() :
	BaseVkApp()
{
}

void VkApp::initVk() {
	BaseVkApp::initVk();

	initDescriptors();
	/*
	createSimplePipeline(simplePipelineStuff, *simpleRenderPass.pass);


	std::vector<float> verts = {
		1,1,0,
		-1,1,0,
		0,-1,0
	};
	std::vector<float> uvs = {
		1,1,
		0.f, 1.f,
		0.5f, 0.0f,
	};
	std::vector<uint32_t> inds = { 0, 1, 2 };
	simpleMesh.fill(3, verts, uvs, {}, inds);
	// simpleMesh.rowSize = 4*(3+2);
	simpleMesh.createAndUpload(deviceGpu,*pdeviceGpu,queueFamilyGfxIdxs);
	createTexturedPipeline(texturedPipelineStuff, simpleMesh, *simpleRenderPass.pass);
	*/


}







void VkApp::initDescriptors() {
	float viewProj[16];
	for (int i=0; i<16; i++) viewProj[i] = i % 5 == 0;
	camBuffer.setAsUniformBuffer(16*4, true);
	camBuffer.create(deviceGpu,*pdeviceGpu,queueFamilyGfxIdxs);
	camBuffer.upload(viewProj, 16*4);


	uint8_t texData[256*256*4];
	for (int y=0; y<256; y++)
	for (int x=0; x<256; x++) {
		for (int c=0; c<3; c++)
			texData[y*256*4+x*4+c] = ((y/8+x/8) % 2) * 100 + 155;
		texData[y*256*4+x*4+3] = ((y/32+x/32)%2) * 100 + 155;
	}
	myTex.createAsTexture(uploader, 256,256, vk::Format::eR8G8B8A8Unorm, texData);

    /* DescriptorPoolCreateInfo( VULKAN_HPP_NAMESPACE::DescriptorPoolCreateFlags  flags_         = {},
                                uint32_t                                         maxSets_       = {},
                                uint32_t                                         poolSizeCount_ = {},
                                const VULKAN_HPP_NAMESPACE::DescriptorPoolSize * pPoolSizes_ = {} ) VULKAN_HPP_NOEXCEPT
	 
	VULKAN_HPP_CONSTEXPR DescriptorSetLayoutCreateInfo(
      VULKAN_HPP_NAMESPACE::DescriptorSetLayoutCreateFlags     flags_        = {},
      uint32_t                                                 bindingCount_ = {},
      const VULKAN_HPP_NAMESPACE::DescriptorSetLayoutBinding * pBindings_    = {} ) VULKAN_HPP_NOEXCEPT

    VULKAN_HPP_CONSTEXPR DescriptorSetLayoutBinding(
      uint32_t                               binding_            = {},
      VULKAN_HPP_NAMESPACE::DescriptorType   descriptorType_     = VULKAN_HPP_NAMESPACE::DescriptorType::eSampler,
      uint32_t                               descriptorCount_    = {},
      VULKAN_HPP_NAMESPACE::ShaderStageFlags stageFlags_         = {},
      const VULKAN_HPP_NAMESPACE::Sampler *  pImmutableSamplers_ = {} ) VULKAN_HPP_NOEXCEPT

      VULKAN_HPP_NAMESPACE::DescriptorPool              descriptorPool_     = {},
      uint32_t                                          descriptorSetCount_ = {},
      const VULKAN_HPP_NAMESPACE::DescriptorSetLayout * pSetLayouts_        = {} ) VULKAN_HPP_NOEXCEPT



    VULKAN_HPP_CONSTEXPR DescriptorBufferInfo( VULKAN_HPP_NAMESPACE::Buffer     buffer_ = {},
                                               VULKAN_HPP_NAMESPACE::DeviceSize offset_ = {},
                                               VULKAN_HPP_NAMESPACE::DeviceSize range_  = {} ) VULKAN_HPP_NOEXCEPT

    VULKAN_HPP_CONSTEXPR WriteDescriptorSet(
      VULKAN_HPP_NAMESPACE::DescriptorSet  dstSet_                    = {},
      uint32_t                             dstBinding_                = {},
      uint32_t                             dstArrayElement_           = {},
      uint32_t                             descriptorCount_           = {},
      VULKAN_HPP_NAMESPACE::DescriptorType descriptorType_            = VULKAN_HPP_NAMESPACE::DescriptorType::eSampler,
      const VULKAN_HPP_NAMESPACE::DescriptorImageInfo *  pImageInfo_  = {},
      const VULKAN_HPP_NAMESPACE::DescriptorBufferInfo * pBufferInfo_ = {},
      const VULKAN_HPP_NAMESPACE::BufferView *           pTexelBufferView_ = {} ) VULKAN_HPP_NOEXCEPT

	  */

	vk::DescriptorPoolSize poolSizes[2] = {
		vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 10 },
		vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 10 },
	};
	vk::DescriptorPoolCreateInfo poolInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
		10,
		2, poolSizes
	};
	descPool = std::move(vk::raii::DescriptorPool(deviceGpu, poolInfo));


	// Uniform buffer for camera matrix
	{
		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0,
			vk::DescriptorType::eUniformBuffer,
			1,
			vk::ShaderStageFlagBits::eVertex }
		};

		vk::DescriptorSetLayoutCreateInfo layInfo {
			{}, 1, bindings
		};
		globalDescLayout = std::move(deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo {
			*descPool, 1, &*globalDescLayout
		};
		globalDescSet = std::move(deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		vk::DescriptorBufferInfo binfo {
			*camBuffer.buffer, 0, 16*4
		};
		vk::WriteDescriptorSet writeDesc[1] = { {
			*globalDescSet,
				0,
				0,
				1,
				vk::DescriptorType::eUniformBuffer,
				nullptr,
				&binfo,
				nullptr } };
		deviceGpu.updateDescriptorSets({1, writeDesc}, nullptr);
	}

	// Create texture stuff
	{
		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0, vk::DescriptorType::eCombinedImageSampler,
			1, vk::ShaderStageFlagBits::eFragment }
		};

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, 1, bindings };
		texDescLayout = std::move(deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *descPool, 1, &*texDescLayout };
		texDescSet = std::move(deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		//vk::DescriptorBufferInfo binfo { *camBuffer.buffer, 0, 9*4 };
		//vk::DescriptorImageInfo iinfo { sampler, myTex.view, myTex.layout };
		vk::DescriptorImageInfo iinfo { *myTex.sampler, *myTex.view, vk::ImageLayout::eShaderReadOnlyOptimal };


		vk::WriteDescriptorSet writeDesc[1] = { {
			*texDescSet,
				0,
				0,
				1,
				vk::DescriptorType::eCombinedImageSampler,
				&iinfo,
				nullptr,
				nullptr } };
		deviceGpu.updateDescriptorSets({1, writeDesc}, nullptr);

	}


	deviceGpu.waitIdle();
}

void VkApp::render() {
	if (not headless) bool proc = pollWindowEvents();


	if (isDone() or frameDatas.size() == 0) {
		//printf(" - [render] frameDatas empty, skipping.\n");
		return;
	}

	vk::Rect2D aoi {
		{ 0, 0 },
		{ windowWidth, windowHeight }
	};
	vk::ClearValue clears_[2] = {
		vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.05f,.1f,1.f } } }, // color
		vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 1.f,1.f,1.f,1.f } } }  // depth
	};

	/*
	uint32_t scIndx = sc.acquireNextImage(0, *scAcquireSema).second;
	float t = this->time();
	float dt = t - lastTime;
	if (lastTime == 0) dt = 0;
	else fpsMeter = fpsMeter * .95 + .05 * (1. / dt);
	lastTime = t;
	camera->step(dt);
	*/

	FrameData& fd = acquireFrame();

	auto& cmd = fd.cmd;

	// camera->step(fd.dt);
	// camera->step(1.0 / 60.0);
	if (fpsMeter > .0000001)
		camera->step(1.0 / fpsMeter);

	// Update Camera Buffer
	if (1) {
		renderState.frameBegin(&fd);
		// void* dbuf = (void*) camBuffer.mem.mapMemory(0, 16*4, {});
		// memcpy(dbuf, renderState.mvp(), 16*4);
		// camBuffer.mem.unmapMemory();
	}

	auto cmds = doRender(renderState);


	executeCommandsThenPresent(cmds, renderState);


	postRender();
}


/*
void VkApp::handleKey(uint8_t key, uint8_t mod, bool isDown) {
	if (!isDown and key == VKK_Q) {
		isDone_ = true;

		//deviceGpu.waitIdle();
		//frameDatas.clear();
		//deviceGpu.waitIdle();
	}
}
*/
bool VkApp::handleKey(int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_Q and action == GLFW_PRESS) isDone_ = true;
	return false;
}



bool VkApp::createSimplePipeline(PipelineStuff& out, vk::RenderPass pass) {
	out.setup_viewport(windowWidth, windowHeight);

	std::string vsrcPath = "../frastVk/shaders/simple.v.glsl";
	std::string fsrcPath = "../frastVk/shaders/simple.f.glsl";
	createShaderFromFiles(deviceGpu, out.vs, out.fs, vsrcPath, fsrcPath);

	PipelineBuilder builder;
	builder.init(
			{},
			vk::PrimitiveTopology::eTriangleList,
			*out.vs, *out.fs);

	return out.build(builder, deviceGpu, pass, 0);
}

bool VkApp::createTexturedPipeline(PipelineStuff& out, ResidentMesh& mesh, vk::RenderPass pass) {
	texturedPipelineStuff.setup_viewport(windowWidth, windowHeight);

	std::string vsrcPath = "../frastVk/shaders/textured.v.glsl";
	std::string fsrcPath = "../frastVk/shaders/textured.f.glsl";
	createShaderFromFiles(deviceGpu, out.vs, out.fs, vsrcPath, fsrcPath);

	PipelineBuilder builder;
	VertexInputDescription vertexInputDescription = mesh.getVertexDescription();
	builder.init(
			vertexInputDescription,
			vk::PrimitiveTopology::eTriangleList,
			*out.vs, *out.fs);

	// Add Push Constants & Set Layouts.
	{
		vk::PushConstantRange pushMvp;
		pushMvp.offset = 0;
		pushMvp.size = sizeof(MeshPushContants);
		pushMvp.stageFlags = vk::ShaderStageFlagBits::eVertex;
		out.pushConstants.push_back(pushMvp);

		//assert(globalDescLayout != nullptr);
		out.setLayouts.push_back(*globalDescLayout);
		out.setLayouts.push_back(*texDescLayout);
	}

	return out.build(builder, deviceGpu, pass, 0);
}

















static std::vector<uint32_t>
compileSource(
		const std::string& source,
		vk::ShaderStageFlagBits stage) {
	char command0[512];

	std::string type = "WHAT";
	switch(stage) {
		case vk::ShaderStageFlagBits::eVertex:
			type = "vert";
			break;
		case vk::ShaderStageFlagBits::eFragment:
			type = "frag";
			break;
		case vk::ShaderStageFlagBits::eCompute:
			type = "comp";
			break;
		default:
			throw std::runtime_error("unk stage.");
	}
	std::string path = "/tmp/shader";

	sprintf(command0, "glslangValidator --stdin -S %s -V -o %s << END\n", type.c_str(), path.c_str());
	std::string command { command0 };
	command = command + source + "\nEND";

	if (system(command.c_str()))
		throw std::runtime_error("Error running glslangValidator command");
	std::ifstream fileStream(path, std::ios::binary);
	std::vector<char> buffer;
	buffer.insert(buffer.begin(), std::istreambuf_iterator<char>(fileStream), {});
	return {(uint32_t*)buffer.data(), (uint32_t*)(buffer.data() + buffer.size())};
}



bool createShaderFromStrings(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vsrc, const std::string& fsrc) {
	fmt::print(" - Deprecated: use createShaderFromSpirv and pysrc/compile_shaders instead.\n");

	std::vector<uint32_t> vs_spirv = compileSource(vsrc, vk::ShaderStageFlagBits::eVertex);
	std::vector<uint32_t> fs_spirv = compileSource(fsrc, vk::ShaderStageFlagBits::eFragment);

	vk::ShaderModuleCreateInfo vs_info { {}, 4*vs_spirv.size(), vs_spirv.data() };
	vk::ShaderModuleCreateInfo fs_info { {}, 4*fs_spirv.size(), fs_spirv.data() };

	vs = std::move(dev.createShaderModule(vs_info));
	fs = std::move(dev.createShaderModule(fs_info));

	return false;
}

// TODO: This does extra read->write->read. instead, skip
bool createShaderFromFiles(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		const std::string& vsrcPath, const std::string& fsrcPath) {
	std::stringstream vsrc, fsrc;
	std::ifstream vi(vsrcPath);
	std::ifstream fi(fsrcPath);
	if (not vi.good()) return true;
	if (not fi.good()) return true;
	vsrc << vi.rdbuf();
	fsrc << fi.rdbuf();
	return createShaderFromStrings(dev, vs,fs, vsrc.str(), fsrc.str());
}

bool createShaderFromSpirv(
		vk::raii::Device& dev,
		vk::raii::ShaderModule& vs, vk::raii::ShaderModule& fs,
		size_t v_spirv_len, size_t f_spirv_len,
		const char* v_spirv, const char* f_spirv) {


	vk::ShaderModuleCreateInfo vs_info { {}, v_spirv_len, (uint32_t*) v_spirv };
	vk::ShaderModuleCreateInfo fs_info { {}, f_spirv_len, (uint32_t*) f_spirv };

	vs = std::move(dev.createShaderModule(vs_info));
	fs = std::move(dev.createShaderModule(fs_info));

	return false;
}
