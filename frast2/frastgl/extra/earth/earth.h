#pragma once

// #include "frastgl/core/window.h"
#include "frast2/frastgl/core/shader.h"

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
