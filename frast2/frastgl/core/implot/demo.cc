#include "frast2/frastgl/core/imgui/imgui_app.h"
#include "frast2/frastgl/core/implot/generated/implot.h"
#include "frast2/frastgl/core/render_state.h"

using namespace frast;
namespace {
	AppConfig appcfg() {
		AppConfig cfg;
		cfg.w = 1920;
		cfg.h = 1080;
		cfg.useImplot = true;
		return cfg;
	}

	class DemoApp : public ImguiApp {
		public:

		inline DemoApp() : ImguiApp(appcfg()) {
		}
		inline ~DemoApp() {
		}

		inline virtual void prepareUi(const RenderState& rs) override {
			ImPlot::ShowDemoWindow();
		}
		inline virtual void render(RenderState& rs) override {
			window.beginFrame();
			renderUi(rs);
			window.endFrame();
		}
		inline virtual void doInit() override {
		}
	};
}

int main() {

	DemoApp app;
	app.init();
	while (1) {
		usleep(10'000);

		RenderState rs;
		app.render(rs);
	}

	return 0;
}
