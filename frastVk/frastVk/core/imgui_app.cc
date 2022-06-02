#include "imgui_app.h"
#include <fmt/core.h>

static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

ImguiApp::ImguiApp() {
}

ImguiApp::~ImguiApp() {
	//if (not headless) ImGui_ImplVulkanH_DestroyWindow(*instance, *deviceGpu, &imguiWindow, nullptr);
	deviceGpu.waitIdle();
	// ImGui_ImplVulkan_DestroyDeviceObjects();
    ImGui_ImplVulkan_Shutdown();
	uiFramebuffers.clear();
	uiPass = nullptr;
	uiDescPool = nullptr;
}

extern void ImGui_ImplVulkanH_CreateWindowCommandBuffers(VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator);

void ImguiApp::initVk() {
	BaseVkApp::initVk();

	if (headless) return;

	vk::CommandPoolCreateInfo poolInfo { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyGfxIdxs[0] };
	cmdPool = std::move(deviceGpu.createCommandPool(poolInfo));
	vk::CommandBufferAllocateInfo bufInfo { *cmdPool, vk::CommandBufferLevel::ePrimary, (uint32_t)scNumImages };
	cmdBuffers = std::move(deviceGpu.allocateCommandBuffers(bufInfo));

	{
		vk::DescriptorPoolSize pool_sizes[] =
        {
            { vk::DescriptorType::eSampler, 100 },
            { vk::DescriptorType::eCombinedImageSampler, 100 },
            { vk::DescriptorType::eSampledImage, 100 },
            { vk::DescriptorType::eStorageImage, 100 },
            { vk::DescriptorType::eUniformTexelBuffer, 100 },
            { vk::DescriptorType::eStorageTexelBuffer, 100 },
            { vk::DescriptorType::eUniformBuffer, 100 },
            { vk::DescriptorType::eUniformBufferDynamic, 100 },
            { vk::DescriptorType::eStorageBufferDynamic, 100 },
            { vk::DescriptorType::eInputAttachment, 100 },
            { vk::DescriptorType::eStorageBuffer, 100 }
        };
		vk::DescriptorPoolCreateInfo pool_info = {};
        // pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        pool_info.maxSets = 100 * std::size(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
		uiDescPool = std::move(vk::raii::DescriptorPool(deviceGpu, pool_info));
		/*
        auto err = vkCreateDescriptorPool(*deviceGpu, &pool_info, nullptr, (VkDescriptorPool*)&*uiDescPool);
        check_vk_result(err);
		*/
	}

	auto wd = &imguiWindow;
    wd->Surface = *surface;
    wd->SurfaceFormat = static_cast<VkSurfaceFormatKHR>(scSurfaceFormat);
    // wd->PresentMode = VkPresentModeKHR { (int) presentMode };
    wd->PresentMode = static_cast<VkPresentModeKHR> ( presentMode );
	wd->ImageCount = scNumImages;
	wd->RenderPass = (VkRenderPass) *simpleRenderPass.pass;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    ImGui::StyleColorsDark();

	{
		vk::AttachmentDescription colorAttachment {
			{},
				scSurfaceFormat.format,
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal,
				// vk::ImageLayout::eColorAttachmentOptimal
				vk::ImageLayout::ePresentSrcKHR
		};

		vk::AttachmentReference colorAttachmentRef { 0, vk::ImageLayout::eColorAttachmentOptimal };

		///*
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
				vk::AttachmentLoadOp::eNoneEXT,
				vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eClear,
				vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eDepthStencilAttachmentOptimal };

		vk::AttachmentReference depthAttachmentRef {
			1,
				vk::ImageLayout::eDepthStencilAttachmentOptimal
		};

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
		};
		//*/

		std::vector<vk::SubpassDescription> subpasses {
			// { {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, {} },
			{ {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef },
				//{ {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef }
		};

		std::vector<vk::AttachmentDescription> atts = {
		 colorAttachment, depthAttachment
		 // colorAttachment
		};
		vk::RenderPassCreateInfo rpInfo {
			{},
				{ (uint32_t)atts.size(), atts.data() },
				// { 1, &subpass0 },
				subpasses,
				{ dependencies }
				// { 2u, dependencies }
				// {}
		};
		uiPass = std::move(deviceGpu.createRenderPass(rpInfo));

		for (int i=0; i<scNumImages; i++) {
			std::vector<vk::ImageView> views = { getSwapChainImageView(i), *simpleRenderPass.depthImages[i].view };
			// std::vector<vk::ImageView> views = { getSwapChainImageView(i) };
			vk::FramebufferCreateInfo fbInfo {
				{},
					*uiPass,
					{ views },
					windowWidth, windowHeight,
					1
			};
			uiFramebuffers.push_back(std::move(deviceGpu.createFramebuffer(fbInfo)));
		}
	}

    ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = *instance;
    init_info.PhysicalDevice = *pdeviceGpu;
    init_info.Device = *deviceGpu;
    init_info.QueueFamily = queueFamilyGfxIdxs[0];
    init_info.Queue = *queueGfx;
    // init_info.PipelineCache = *uiPipelineCache;
    init_info.PipelineCache = nullptr;
    init_info.DescriptorPool = *uiDescPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = scNumImages;
    init_info.ImageCount = wd->ImageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info, wd->RenderPass);

	fmt::print(" - Calling ImGui_ImplVulkanH_CreateWindowCommandBuffers\n");
	//ImGui_ImplVulkanH_CreateWindowCommandBuffers(*pdeviceGpu, *deviceGpu, wd, queueFamilyGfxIdxs[0], nullptr);
	fmt::print(" - Calling ImGui_ImplVulkanH_CreateWindowCommandBuffers ... done\n");

    // Upload Fonts
    {
        // Use any command queue
        //VkCommandPool command_pool = wd->Frames[wd->FrameIndex].CommandPool;
        //VkCommandBuffer command_buffer = wd->Frames[wd->FrameIndex].CommandBuffer;
        VkCommandBuffer command_buffer = *frameDatas[0].cmd;

        // auto err0 = vkResetCommandPool(*deviceGpu, command_pool, 0);
        // check_vk_result(err0);
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        auto err = vkBeginCommandBuffer(command_buffer, &begin_info);
        check_vk_result(err);

        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        err = vkEndCommandBuffer(command_buffer);
        check_vk_result(err);
        err = vkQueueSubmit(*queueGfx, 1, &end_info, VK_NULL_HANDLE);
        check_vk_result(err);

        err = vkDeviceWaitIdle(*deviceGpu);
        check_vk_result(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

}

void ImguiApp::render() {

	if (isDone() or frameDatas.size() == 0) {
		return;
	}

	if (not headless) {
		bool proc = pollWindowEvents();
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		if (showMenu) {
		prepareUi(renderState);
		}
	}



	FrameData& fd = acquireFrame();
	if (fpsMeter > .0000001) camera->step(1.0 / fpsMeter);

	// Update Camera Buffer
	if (1) {
		renderState.frameBegin(&fd);
	}
	auto cmds = doRender(renderState);



	if (not headless) {
		vk::raii::CommandBuffer &uiCmd = cmdBuffers[fd.scIndx];
		uiCmd.reset();
		uiCmd.begin({});

		vk::Rect2D aoi { { 0, 0 }, { windowWidth, windowHeight } };
		vk::RenderPassBeginInfo rpInfo {
			*uiPass, *uiFramebuffers[renderState.frameData->scIndx],
			aoi, {}
		};

		if (showMenu) {
			uiCmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
			renderUi(renderState, *uiCmd);
			uiCmd.endRenderPass();

		} else {

			ImGui::Render();

			vk::ImageMemoryBarrier barrier;
			barrier.image = sc.getImage(renderState.frameData->scIndx);
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
			barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
			uiCmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		}

		uiCmd.end();
		cmds.push_back(*uiCmd);
	}

	/*
	vk::ImageMemoryBarrier barrier;
	barrier.image = sc.getImage(renderState.frameData->scIndx);
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
	barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
	*/


	executeCommandsThenPresent(cmds, renderState);

	postRender();
}

bool ImguiApp::handleKey(int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_Q and action == GLFW_PRESS) isDone_ = true;
	return false;
}
bool ImguiApp::handleMousePress(int button, int action, int mods) {
	// Don't do anything if control is pressed. This helps interact only with menu!
	if (mods & GLFW_MOD_CONTROL) return true;
	return false;
}

void ImguiApp::prepareUi(RenderState& rs) {
    ImGui::ShowDemoWindow(&showMenu);
}
void ImguiApp::renderUi(RenderState& rs, vk::CommandBuffer cmd) {
    ImGui::Render();
	ImDrawData* draw_data = ImGui::GetDrawData();
	const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
}
