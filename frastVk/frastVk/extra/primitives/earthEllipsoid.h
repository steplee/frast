#pragma once

#include "frastVk/core/fvkApi.h"


struct EarthEllipsoid {

	inline EarthEllipsoid(BaseApp* app) : app(app) { init(); }

		void render(RenderState& rs, Command& cmd);
		void init();

	private:

		BaseApp* app;

		DescriptorSet descSet;
		GraphicsPipeline pipeline;

		// Stores inverse mvp with focal lengths
		ExBuffer globalBuffer;
};

