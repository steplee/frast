#include "imgui_app.h"

#include "generated/imgui.h"
#include "generated/imgui_impl_glfw.h"
#include "generated/imgui_impl_opengl3.h"

namespace frast {


ImguiApp::ImguiApp(const AppConfig& cfg)
	: App(cfg)
{
}

void ImguiApp::init() {
	App::init();
	initUi();
}

void ImguiApp::prepareUi(const RenderState& rs) {
	bool show_demo_window = true;
    ImGui::ShowDemoWindow(&show_demo_window);
}

void ImguiApp::initUi() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window.glfwWindow, true);
    ImGui_ImplOpenGL3_Init("#version 440");
}

void ImguiApp::renderUi(const RenderState& rs) {
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Call user-overloaded virtual function
	prepareUi(rs);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	ImGui::Render();
	// int display_w, display_h;
	// glfwGetFramebufferSize(window.glfwWindow, &display_w, &display_h);
	// glViewport(0, 0, display_w, display_h);
	// glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
	// glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}
