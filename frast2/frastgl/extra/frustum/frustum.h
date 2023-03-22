#pragma once

// #include "frastgl/core/window.h"
#include "frast2/frastgl/core/shader.h"

#include "frast2/frastgl/core/render_state.h"

#include "frast2/frastgl/extra/ellipsoid/ellipsoid.h"

#include <memory>

namespace frast {

class Castable;

class Frustum {

	public:

		Frustum();

		void render(const RenderState& rs);

		// void setSpec(const CameraSpec& s);
		void setProj(const CameraSpec& spec);
		void setProj(const double* projInv);
		void setPose(const double* viewInv, bool pushTrail);
		void setColor(const float c[4]);

		void resetTrail();

		Ellipsoid* getOrCreateEllipsoid();

		// A Frustum is often used with a "casted" texture, if the Gt impl supports casting.
		// This is a helper function to get the caster matrix.
		//
		// The caster matrix is the forward projection (which requires proj*view)
		void getCasterMatrix(float* out) const;
		void getModelMatrix(double* out) const;


	protected:
		// CameraSpec spec;

		float color[4];
		double viewInv[16];
		double projInv[16];
		double view[16];
		double proj[16];

		std::unique_ptr<Ellipsoid> ellps{};

		GLuint trailVbo;
		// GLuint trailBackupVbo;
		static constexpr int trailLength = 4096;
		static constexpr int trailSize       = 3*4*4096;
		// float trailData[trailSize];
		float *trailData; // the map of the vbo
		int trailIndex;

};

}
