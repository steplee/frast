#pragma once

// #include "frastgl/core/window.h"
#include "frastgl/core/shader.h"

namespace frast {

class RenderState;

class EarthEllipsoid {
	public:

		EarthEllipsoid();

		void render(const RenderState& rs);

	private:

		Shader shader;



};

}
