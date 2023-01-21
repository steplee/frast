#pragma once

#include "window.h"

namespace frast {

class RenderState;

struct AppConfig {
	std::string title;
	int w=512, h=512;
	bool headless = false;
};

class App : public UsesIO {
	public:
		App(const AppConfig& cfg);

		void init();

		virtual void render(RenderState& rs) =0;
		virtual void doInit() =0;


	protected:
		AppConfig cfg;
		MyGlfwWindow window;
};


}
