#pragma once

// #include "frastgl/core/window.h"
#include "frast2/frastgl/core/shader.h"

#include "frast2/frastgl/core/render_state.h"

#include "frast2/frastgl/extra/ellipsoid/ellipsoid.h"

namespace frast {

class Ellipsoid {

	public:

		Ellipsoid();

		void render(const RenderState& rs);

		void setColor(const float c[4]);
		void setModel(const float M[16]);
		void setModelFromInvViewAndCov(const float VI[16], const float C[9]);

	protected:
		// CameraSpec spec;

		float color[4];
		float M[16];

		// FIXME: Consider caching shaders to avoid duplicates..
		Shader ellpsShader;

		std::vector<float> verts; // Have positions (normals = positions for a sphere)
		std::vector<uint16_t> inds;

};

}

