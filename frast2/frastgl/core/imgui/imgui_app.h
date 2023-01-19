#pragma once

#include "frast2/frastgl/core/app.h"

#include "frast2/frastgl/core/imgui/generated/imgui.h"

namespace frast {

class ImguiApp : public App {
	public:

		ImguiApp(const AppConfig& cfg);

		// virtual void render(RenderState& rs) =0;
		// virtual void doInit() =0;


		// Note: This shadows App::init() (and calls it internally)
		void init();

	protected:

		virtual void prepareUi(const RenderState& rs);

		void renderUi(const RenderState& rs);

	private:

		void initUi();

};
}
