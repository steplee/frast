#pragma once

#include "window.h"

#define glCheck(x) { (x); auto e = glGetError(); if (e != 0) throw std::runtime_error(fmt::format("gl call {} failed with {:x}", #x, e)); }

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
