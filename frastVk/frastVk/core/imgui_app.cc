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

ImguiApp::ImguiApp(const AppConfig& cfg) : BaseApp(cfg) {}

ImguiApp::~ImguiApp() {
	//if (not headless) ImGui_ImplVulkanH_DestroyWindow(*instance, *deviceGpu, &imguiWindow, nullptr);
	// deviceGpu.waitIdle();
	// ImGui_ImplVulkan_DestroyDeviceObjects();
    ImGui_ImplVulkan_Shutdown();

	if (uiPass) vkDestroyRenderPass(mainDevice, uiPass, nullptr);
	if (uiDescPool) vkDestroyDescriptorPool(mainDevice, uiDescPool, nullptr);

	// uiFramebuffers.clear();
	uiPass = nullptr;
	uiDescPool = nullptr;

}

extern void ImGui_ImplVulkanH_CreateWindowCommandBuffers(VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator);

void ImguiApp::initVk() {
	BaseApp::initVk();

	if (cfg.headless()) return;

	{
		VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
        };
		VkDescriptorPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 100 * std::size(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        assertCallVk(vkCreateDescriptorPool(mainDevice, &pool_info, nullptr, (VkDescriptorPool*)&uiDescPool));
	}

	auto wd = &imguiWindow;
    wd->Surface = swapchain.surface;
    wd->SurfaceFormat = static_cast<VkSurfaceFormatKHR>(swapchain.surfFormat);
    // wd->PresentMode = VkPresentModeKHR { (int) presentMode };
    wd->PresentMode = static_cast<VkPresentModeKHR> ( swapchain.presentMode );
	wd->ImageCount = swapchain.numImages;
	wd->RenderPass = (VkRenderPass) simpleRenderPass.pass;
	// wd->RenderPass = (VkRenderPass) uiPass;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    ImGui::StyleColorsDark();

	uint32_t width = cfg.width;
	uint32_t height = cfg.height;

	// Copy the App's simpleRenderPass, but we since we render UI after app does doRender(), and we use the same framebuffers,
	// we don't want to clear them. So we have to modify the RenderPassDescription.
	// Also, the initial layout for simpleRenderPass is undefined, but here it should be color or depth attachment optimal.
	{
		RenderPassDescription descriptionCopy = simpleRenderPass.description;
		for (auto &d : descriptionCopy.attDescriptions) {
			if (d.format == VK_FORMAT_D16_UNORM or
				d.format == VK_FORMAT_D32_SFLOAT or
				d.format == VK_FORMAT_D16_UNORM or
				d.format == VK_FORMAT_D24_UNORM_S8_UINT or
				d.format == VK_FORMAT_D32_SFLOAT)
				d.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			else
				d.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		}
		auto rpInfo = descriptionCopy.makeCreateInfo();
		assertCallVk(vkCreateRenderPass(mainDevice, &rpInfo, nullptr, &uiPass));

		/*
		uiFramebuffers.resize(swapchain.numImages);
		for (int i=0; i<swapchain.numImages; i++) {
			VkImageView views[2] = { swapchain.images[i].view, simpleRenderPass.depthImages[i].view };
			VkFramebufferCreateInfo fbInfo {
				VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr,
				{},
				simpleRenderPass.pass,
				2,
				views,
				width, height,
				1
			};
			assertCallVk(vkCreateFramebuffer(mainDevice, &fbInfo, nullptr, &uiFramebuffers[i]));
		}
		*/
	}


    ImGui_ImplGlfw_InitForVulkan(glfwWindow->glfwWindow, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = mainDevice.pdevice;
    init_info.Device = mainDevice.device;
    init_info.QueueFamily = mainDevice.queueFamilyGfxIdxs[0];
    init_info.Queue = mainQueue;
    init_info.PipelineCache = nullptr;
    // init_info.PipelineCache = *uiPipelineCache;
    init_info.PipelineCache = nullptr;
    init_info.DescriptorPool = uiDescPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = swapchain.numImages;
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
        VkCommandBuffer command_buffer = frameDatas[0].cmd;

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
        err = vkQueueSubmit(mainQueue, 1, &end_info, VK_NULL_HANDLE);
        check_vk_result(err);

        err = vkDeviceWaitIdle(mainDevice);
        check_vk_result(err);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

}

void ImguiApp::render() {

	if (isDone() or frameDatas.size() == 0) {
		return;
	}

	if (not cfg.headless()) {
		bool proc = glfwWindow->pollWindowEvents();
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		if (showMenu) {
		prepareUi(renderState);
		}
	}



	FrameData& fd = acquireNextFrame();
	if (fpsMeter > .0000001) camera->step(1.0 / fpsMeter);

	// Update Camera Buffer
	if (1) {
		renderState.camera = camera.get();
		renderState.frameBegin(&fd);
	}

	doRender(renderState);


	if (not cfg.headless()) {
		auto &uiCmd = fd.cmd;
		//uiCmd.reset();
		// uiCmd.begin({});

		VkClearValue clearValues[2] = {
			VkClearValue { .color = VkClearColorValue { .float32 = {0.f,0.f,0.f,0.f} } },
			VkClearValue { .depthStencil = VkClearDepthStencilValue { 0.f, 0 } }
		};

		VkRect2D aoi { { 0, 0 }, { windowWidth, windowHeight } };
		VkRenderPassBeginInfo rpInfo {
			VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr,
			// uiPass, uiFramebuffers[renderState.frameData->scIndx],
			uiPass, simpleRenderPass.framebuffers[fd.scIndx],
			aoi,
			// 2, clearValues
			0, nullptr
		};


		if (showMenu) {
			vkCmdBeginRenderPass(uiCmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
			renderUi(renderState, uiCmd);
			vkCmdEndRenderPass(uiCmd);

		} else {

			ImGui::Render();

			/*
			Barriers barriers;
			auto& img = swapchain.images[renderState.frameData->scIndx];
			barriers.append(img, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			cmd.barriers(barriers);
			*/
		}

		// uiCmd.end();
		// cmds.push_back(*uiCmd);
	}

	DeviceQueueSpec dqs { mainDevice, mainQueue };
	fd.cmd.executeAndPresent(dqs, swapchain, fd);

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
void ImguiApp::renderUi(RenderState& rs, Command& cmd) {
    ImGui::Render();
	ImDrawData* draw_data = ImGui::GetDrawData();
	const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
}

