#include "app.h"

namespace frast {
	App::App(const AppConfig& cfg)
		: cfg(cfg),
		  window(cfg.w, cfg.h, cfg.headless)
	{
	}

	void App::init() {

		window.setupWindow();

		window.addIoUser(this);

		doInit();

	}

}
