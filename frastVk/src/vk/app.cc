#include "app.h"

#include <vulkan/vulkan_xcb.h>
#include <iostream>
#include <fstream>
#include <chrono>

#include "clipmap1/clipmap1.h"
#include "tiled_renderer/tiled_renderer.h"


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

/* ===================================================
 *
 *
 *                  BaseVkApp
 *
 *
 * =================================================== */

bool BaseVkApp::make_instance() {

	vk::ApplicationInfo       applicationInfo{
			"vulkanApp",
			VK_MAKE_VERSION(1, 0, 0),
			"vulkanAppEngine",
			VK_MAKE_VERSION(1, 0, 0),
			//VK_MAKE_VERSION(1, 2, 0) };
			VK_MAKE_VERSION(2, 0, 3) };

	std::vector<char*> layers_ = {
#ifdef VULKAN_DEBUG
		"VK_LAYER_KHRONOS_validation",
		//"VK_LAYER_LUNARG_parameter_validation",
      	//"VK_LAYER_LUNARG_device_limits", 
      	//"VK_LAYER_LUNARG_object_tracker",
      	//"VK_LAYER_LUNARG_image",
      	//"VK_LAYER_LUNARG_core_validation",
#endif
	};
	std::vector<char*>  extensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_XCB_SURFACE_EXTENSION_NAME
		//,VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME 
#ifdef VULKAN_DEBUG
		,VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		,VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
	};

	vk::InstanceCreateInfo info {
			{},
			&applicationInfo,
			layers_,
			extensions
	};

	std::cout << " - Using layers:\n";
	for (auto l : layers_) std::cout << l << "\n";

	instance = vk::raii::Instance(ctx, info);
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
			if (props.deviceType == prefer) {
				myDeviceIdx = i;
				break;
			}
			printf(" - want %u, skipping dev %d of type %u\n", prefer, i, props.deviceType);
		}
	assert(myDeviceIdx < 9999);
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

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(pdevice, &props);
	printf(" - Selected Device '%s', queueFamily %u.\n", props.deviceName, qfamilyIdx);
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
	const std::vector<char*> exts = {
		VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
		,VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME 
		//,VK_KHR_16BIT_STORAGE_EXTENSION_NAME
	};

	void* createInfoNext = nullptr;

	VkPhysicalDeviceVulkan11Features extraFeatures3 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
	};
	extraFeatures3.storageBuffer16BitAccess = true;
	extraFeatures3.uniformAndStorageBuffer16BitAccess = true;
	VkPhysicalDeviceFeatures2 extraFeatures4 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	extraFeatures4.features.shaderInt16 = true;
	extraFeatures3.pNext = &extraFeatures4;

	VkPhysicalDeviceUniformBufferStandardLayoutFeatures bufLayoutFeature = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES
	};
	bufLayoutFeature.uniformBufferStandardLayout = true;
	extraFeatures4.pNext = &bufLayoutFeature;

	VkPhysicalDeviceIndexTypeUint8FeaturesEXT extraFeatures5 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT,
		nullptr,
		true
	};
	bufLayoutFeature.pNext = &extraFeatures5;
	//extraFeatures4.pNext = &extraFeatures5;

	VkPhysicalDeviceUniformBufferStandardLayoutFeatures extraFeatures6 = {
	VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR,
		nullptr,
		true
	};
	extraFeatures5.pNext = &extraFeatures6;

	/*
	if (require_16bit_shader_types) {
		createInfoNext = &extraFeatures3;
	} else
		createInfoNext = &bufLayoutFeature;
	*/
	createInfoNext = &extraFeatures3;


    VkDeviceCreateInfo dinfo {
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			createInfoNext,
			{},
			n_q_family, qinfos,
			0, nullptr,
			(uint32_t)exts.size(), exts.data(),
			nullptr };
	deviceGpu = std::move(vk::raii::Device(pdeviceGpu, dinfo, nullptr));

	//std::cout << " - device version: " << app.deviceGpu.getDispatcher()->getVkHeaderVersion() << " header " << VK_HEADER_VERSION << "\n";

	// Get Queue
	queueGfx = deviceGpu.getQueue(qfamilyIdx, 0);


	return false;
}

bool BaseVkApp::make_surface() {
	vk::XcbSurfaceCreateInfoKHR s_ci = {
		{},
		xcbConn,
		xcbWindow
	};

	surface = std::move(vk::raii::SurfaceKHR{instance, s_ci});
	return false;
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
		vk::PresentModeKHR::eFifo,
		VK_TRUE, nullptr
	};
	scNumImages = 3;

	VkSwapchainKHR sc_;
	sc = std::move(deviceGpu.createSwapchainKHR(sc_ci_));

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

	return false;
}
bool BaseVkApp::make_frames() {

	vk::CommandPoolCreateInfo poolInfo {
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		queueFamilyGfxIdxs[0] };
	commandPool = std::move(deviceGpu.createCommandPool(poolInfo));

	vk::CommandBufferAllocateInfo bufInfo {
		*commandPool,
		vk::CommandBufferLevel::ePrimary,
		(uint32_t)scNumImages };
	std::vector<vk::raii::CommandBuffer> commandBuffers = std::move(deviceGpu.allocateCommandBuffers(bufInfo));

	for (int i=0; i<scNumImages; i++) {
		frameDatas.push_back(FrameData{});
		frameDatas.back().scAcquireSema = std::move(deviceGpu.createSemaphore({}));
		frameDatas.back().renderCompleteSema = std::move(deviceGpu.createSemaphore({}));
		frameDatas.back().frameReadyFence = std::move(deviceGpu.createFence({}));
		frameDatas.back().frameDoneFence = std::move(deviceGpu.createFence({vk::FenceCreateFlagBits::eSignaled}));
		//frameDatas.back().frameReadyFence = std::move(deviceGpu.createFence({}));
		//frameDatas.back().frameDoneFence = std::move(deviceGpu.createFence({}));
		frameDatas.back().cmd = std::move(commandBuffers[i]);
		frameDatas.back().scIndx = i;
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
			//vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR };

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

	vk::SubpassDescription subpass0 {
		{},
			vk::PipelineBindPoint::eGraphics,
			{ },
			{ 1, &colorAttachmentRef },
			{ },
			&depthAttachmentRef
	};


	// Depth dependency
      /*SubpassDependency( uint32_t                                 srcSubpass_      = {},
                         uint32_t                                 dstSubpass_      = {},
                         VULKAN_HPP_NAMESPACE::PipelineStageFlags srcStageMask_    = {},
                         VULKAN_HPP_NAMESPACE::PipelineStageFlags dstStageMask_    = {},
                         VULKAN_HPP_NAMESPACE::AccessFlags        srcAccessMask_   = {},
                         VULKAN_HPP_NAMESPACE::AccessFlags        dstAccessMask_   = {},
                         VULKAN_HPP_NAMESPACE::DependencyFlags    dependencyFlags_ = {} ) VULKAN_HPP_NOEXCEPT*/
	vk::SubpassDependency depthDependency {
		VK_SUBPASS_EXTERNAL, 0,
		vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
		vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
		{},
		vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		{}
	};


	vk::AttachmentDescription atts[2] = { colorAttachment, depthAttachment };
	vk::RenderPassCreateInfo rpInfo {
		{},
		{ 2, atts },
		{ 1, &subpass0 },
		{ 1, &depthDependency }
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
	std::cout << " - initial      : " << *deviceGpu << " " << *surface << "\n";

	make_instance();

	windowWidth = 640;
	windowHeight = 480;
	setupWindow();

	make_gpu_device();

	uploader = std::move(Uploader(this, *queueGfx));

	make_surface();

	make_swapchain();
	make_frames();

	make_basic_render_stuff();


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

		bool depthTest = true, depthWrite = true;
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

	{
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

bool PipelineStuff::build(PipelineBuilder& builder, vk::raii::Device& device, const vk::RenderPass& pass) {
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
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	pipeline = std::move(device.createGraphicsPipeline(nullptr, pipelineInfo));

	return false;
}

float BaseVkApp::time() {
	return getSeconds() - time0;
}

FrameData& BaseVkApp::acquireFrame() {
	int ii1 = renders == 0 ? scNumImages-1 : (renders-1) % scNumImages;
	int ii = renders % scNumImages;

	FrameData& fd = frameDatas[ii];

	deviceGpu.waitForFences({*frameDatas[ii].frameDoneFence}, true, 999999999);
	deviceGpu.resetFences({*frameDatas[ii].frameDoneFence});
	uint32_t new_scIndx = sc.acquireNextImage(0, *fd.scAcquireSema, *fd.frameReadyFence).second;
	deviceGpu.waitForFences({*fd.frameReadyFence}, true, 999999999);
	deviceGpu.resetFences({*fd.frameReadyFence});
	// uint32_t new_scIndx = sc.acquireNextImage(0, *fd.scAcquireSema).second;

	renders++;
	if (renders == 1) {
		time0 = getSeconds(true);
		for (auto& fd : frameDatas) fd.time = time0;
	}
	fd.time = time();
	fd.dt = fd.time - frameDatas[ii1].time;
	fd.n = renders-1;

	if (renders > 1)
		fpsMeter = fpsMeter * .95 + .05 * (1. / fd.dt);
	if (renders % 60 == 0)
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

	createSimplePipeline(simplePipelineStuff, *simpleRenderPass.pass);

	initDescriptors();

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
	//simpleMesh.createAndUpload(uploader);
	simpleMesh.createAndUpload(deviceGpu,*pdeviceGpu,queueFamilyGfxIdxs);
	createTexturedPipeline(texturedPipelineStuff, simpleMesh, *simpleRenderPass.pass);

	//set position of camera offset by loaded mld ctr

	CameraSpec spec { (float)windowWidth, (float)windowHeight, 45 * 3.141 / 180. };
	camera = std::make_shared<MovingCamera>(spec);
	//alignas(16) float pos0[] { 0, 0, .9999f };
	//alignas(16) float pos0[] { 0, 0, (float)(2.0 * 2.38418579e-7) };
	alignas(16) double pos0[] { 
		(double)(-8590834.045999 / 20037508.342789248), (float)(4757669.951554 / 20037508.342789248),
		//0,0,
		//(double)(2.0 * 2.38418579e-7) };
		//(double)(2.0 * 1./(1<<(18-1))) };
		(double)(2.0 * 1./(1<<(10-1))) };
	alignas(16) double R0[] {
		1,0,0,
		0,-1,0,
		0,0,-1 };
	camera->setPosition(pos0);
	camera->setRotMatrix(R0);
	ioUsers.push_back(camera);
	renderState.camera = camera;

	//clipmap = std::make_shared<ClipMapRenderer1>(this);
	//clipmap->init();
	tiledRenderer = std::make_shared<TiledRenderer>(this);
	tiledRenderer->init();
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
	camera->step(fd.dt);



	// TODO: Have just one commandBuffer for one render, then 3 others that blit to current framebuffer.
	if (fd.n == 0) {

		for (int i=0; i<scNumImages; i++) {

			vk::CommandBufferBeginInfo beginInfo {
				//vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
				{},
					{} };

			vk::RenderPassBeginInfo rpInfo {
				*simpleRenderPass.pass,
				*simpleRenderPass.framebuffers[i],
				aoi,
				{2, clears_} };

			vk::raii::CommandBuffer &cmd_ = frameDatas[i].cmd;

			cmd_.reset();
			cmd_.begin(beginInfo);
			cmd_.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

			if (0) {
				cmd_.bindPipeline(vk::PipelineBindPoint::eGraphics, *simplePipelineStuff.pipeline);
				cmd_.draw(3, 1, 0, 0);
			} else {
				cmd_.bindPipeline(vk::PipelineBindPoint::eGraphics, *texturedPipelineStuff.pipeline);
				cmd_.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*simpleMesh.vertBuffer.buffer}, {0u});
				cmd_.bindIndexBuffer(*simpleMesh.indBuffer.buffer, {0u}, vk::IndexType::eUint32);

				MeshPushContants pushed;
				for (int i=0; i<16; i++) pushed.model[i] = i % 5 == 0;
				cmd_.pushConstants(*texturedPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const MeshPushContants>{1, &pushed});

				cmd_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *texturedPipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
				cmd_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *texturedPipelineStuff.pipelineLayout, 1, {1,&*texDescSet}, nullptr);

				cmd_.drawIndexed(3, 1, 0, 0, 0);
			}
			cmd_.endRenderPass();
			cmd_.end();
		}
	}

	vk::raii::CommandBuffer &cmd = fd.cmd;

	// Update Camera Buffer
	if (1) {
		renderState.frameBegin(&fd);
		void* dbuf = (void*) camBuffer.mem.mapMemory(0, 16*4, {});
		memcpy(dbuf, renderState.mvp(), 16*4);
		camBuffer.mem.unmapMemory();
	}

#if 0
	vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;
	vk::SubmitInfo submitInfo {
		1, &(*fd.scAcquireSema), // wait sema
		&waitMask,
		//1, &(*commandBuffers[fd.scIndx]),
		1, &(*cmd),
		1, &*fd.renderCompleteSema // signal sema
	};
	queueGfx.submit(submitInfo, *fd.frameDoneFence);

#else
	std::vector<vk::CommandBuffer> cmds = {
		//clipmap->stepAndRender(renderState, camera.get())
		tiledRenderer->stepAndRender(renderState)
		//,*cmd
	};

	vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;
	vk::SubmitInfo submitInfo {
		1, &(*fd.scAcquireSema), // wait sema
		&waitMask,
		//1, &(*commandBuffers[fd.scIndx]),
		(uint32_t)cmds.size(), cmds.data(),
		1, &*fd.renderCompleteSema // signal sema
	};
	queueGfx.submit(submitInfo, *fd.frameDoneFence);
#endif

	vk::PresentInfoKHR presentInfo {
		1, &*fd.renderCompleteSema, // wait sema
		1, &(*sc),
		&fd.scIndx, nullptr
	};
	queueGfx.presentKHR(presentInfo);
	//isDone_ = true;

	//deviceGpu.waitIdle();

	/*
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRenderPass(cmd);
	*/
}

bool VkApp::isDone() {
	return isDone_;
}

void VkApp::handleKey(uint8_t key, uint8_t mod, bool isDown) {
	if (!isDown and key == VKK_Q) {
		isDone_ = true;

		//deviceGpu.waitIdle();
		//frameDatas.clear();
		//deviceGpu.waitIdle();
	}

}



bool VkApp::createSimplePipeline(PipelineStuff& out, vk::RenderPass pass) {
	simplePipelineStuff.setup_viewport(windowWidth, windowHeight);

	std::string vsrcPath = "../src/shaders/simple.v.glsl";
	std::string fsrcPath = "../src/shaders/simple.f.glsl";
	createShaderFromFiles(deviceGpu, out.vs, out.fs, vsrcPath, fsrcPath);

	PipelineBuilder builder;
	builder.init(
			{},
			vk::PrimitiveTopology::eTriangleList,
			*out.vs, *out.fs);

	return out.build(builder, deviceGpu, pass);
}

bool VkApp::createTexturedPipeline(PipelineStuff& out, ResidentMesh& mesh, vk::RenderPass pass) {
	texturedPipelineStuff.setup_viewport(windowWidth, windowHeight);

	std::string vsrcPath = "../src/shaders/textured.v.glsl";
	std::string fsrcPath = "../src/shaders/textured.f.glsl";
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

	return out.build(builder, deviceGpu, pass);
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
