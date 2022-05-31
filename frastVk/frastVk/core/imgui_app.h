#pragma once

#include "app.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"


class ImguiApp : public BaseVkApp {
	public:
		ImguiApp();
		virtual ~ImguiApp();

		virtual void initVk() override;

		virtual void render();
		virtual std::vector<vk::CommandBuffer> doRender(RenderState& rs) =0;
		inline virtual void postRender() {}
		inline virtual void handleCompletedHeadlessRender(RenderState& rs) {};

		inline virtual uint32_t mainSubpass() const override { return 0; }

		virtual bool handleKey(int key, int scancode, int action, int mods) override;
		virtual bool handleMousePress(int button, int action, int mods) override;
		// virtual void destroyWindow() override;
		
	protected:
		ImGui_ImplVulkanH_Window imguiWindow;

		vk::raii::DescriptorPool uiDescPool { nullptr };
		vk::raii::RenderPass uiPass { nullptr };
		std::vector<vk::raii::Framebuffer> uiFramebuffers;

		bool showMenu = true;

		std::shared_ptr<Camera> camera = nullptr;

		vk::raii::CommandPool cmdPool { nullptr };
		std::vector<vk::raii::CommandBuffer> cmdBuffers;


		virtual void prepareUi(RenderState& rs);
		virtual void renderUi(RenderState& rs, vk::CommandBuffer cmd);
};
