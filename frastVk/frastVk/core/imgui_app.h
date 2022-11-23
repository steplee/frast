#pragma once

#include "fvkApi.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"


class ImguiApp : public BaseApp {
	public:
		ImguiApp(const AppConfig& cfg);
		virtual ~ImguiApp();

		virtual void initVk() override;

		virtual void render() override;
		virtual void doRender(RenderState& rs) =0;
		inline virtual void postRender() override {}

		// inline virtual void handleCompletedHeadlessRender(RenderState& rs) {};


		virtual bool handleKey(int key, int scancode, int action, int mods) override;
		virtual bool handleMousePress(int button, int action, int mods) override;
		// virtual void destroyWindow() override;
		
	protected:
		ImGui_ImplVulkanH_Window imguiWindow;

		VkDescriptorPool uiDescPool { nullptr };
		VkRenderPass uiPass { nullptr };
		// std::vector<VkFramebuffer> uiFramebuffers;

		bool showMenu = true;

		std::shared_ptr<Camera> camera = nullptr;

		virtual void prepareUi(RenderState& rs);
		virtual void renderUi(RenderState& rs, Command& cmd);
};
