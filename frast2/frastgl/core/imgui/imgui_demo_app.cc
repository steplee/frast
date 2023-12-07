#include <unistd.h>

#include "frast2/frastgl/core/app.h"
#include "frast2/frastgl/core/imgui/imgui_app.h"
#include "frast2/frastgl/core/render_state.h"

#include <chrono>
#include <fmt/core.h>
#include <cmath>
#include <signal.h>

using namespace frast;

//
// No need for this to not use the main thread -- except that is an example for future applications.
//


using BaseClass_App = ImguiApp;

class TestApp : public BaseClass_App {
	public:

		inline TestApp(const AppConfig& cfg)
			: BaseClass_App(cfg)
		{
		}

		inline ~TestApp() {
		}

		virtual void render(RenderState& rs) override {
			window.beginFrame();
			glCheck("beginFrame");

			// fmt::print(" - render\n");
			glClearColor(0,0,.01,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glCheck("clear");

			if constexpr (std::is_same_v<BaseClass_App, ImguiApp>) ImguiApp::renderUi(rs);

			window.endFrame();
			glCheck("endFrame");
		}


		virtual void doInit() override {
		}

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override {
			// if (action == GLFW_PRESS and key == GLFW_KEY_M) moveCaster = !moveCaster;
			return false;
		}

	public:


		inline void loop() {
			int frames=0;

			init();

			CameraSpec spec(cfg.w, cfg.h, 45.0 * M_PI/180);
			GlobeCamera cam(spec);
			// SphericalEarthMovingCamera cam(spec);


			window.addIoUser(&cam);

			// FIXME: call glViewport...

			auto last_time = std::chrono::high_resolution_clock::now();

			while (1) {
				usleep(33'000);
				frames++;

				cam.step(0);
				RenderState rs(&cam);
				rs.frameBegin();

				render(rs);

			}

			fmt::print(" - Destroying ftr in render thread\n");
		}

    inline virtual void prepareUi(const RenderState& rs) override {
        // ImGui::ShowDemoWindow(&showUi);

        ImGuiWindowFlags window_flags = 0;
        window_flags |= ImGuiWindowFlags_NoTitleBar;
        window_flags |= ImGuiWindowFlags_NoScrollbar;
        // window_flags |= ImGuiWindowFlags_NoBackground;

		bool show_demo_window = true;
		ImGui::ShowDemoWindow(&show_demo_window);

    }

};



int main() {


	AppConfig appCfg;
	// appCfg.w = 1024;
	appCfg.w = 1920;
	appCfg.h = 1080;

	TestApp app(appCfg);
	app.loop();


	return 0;
}
