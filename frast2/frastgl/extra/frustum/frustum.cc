#include "frustum.h"

#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/core/shader.h"
#include "frast2/frastgl/shaders/earth.h"
#include "frast2/frastgl/utils/eigen.h"

#include <fmt/ostream.h>

using namespace Eigen;

namespace frast {

Frustum::Frustum() {

	CameraSpec spec = CameraSpec(512,512, 45 * M_PI / 180);
	// spec.near = 25. / 6,378'137.0;
	// spec.far = 1000. / 6,378'137.0;
	spec.near = .0001;
	spec.far = .002;
	setProj(spec);

	color[0] = color[1] = color[2] = 1.f;
	color[3] = .5f;

	for (int i=0; i<16; i++) viewInv[i] = (i % 5) == 0;
	for (int i=0; i<16; i++) view[i] = (i % 5) == 0;

}

void Frustum::setColor(const float c[4]) {
	for (int i=0; i<4; i++) color[i] = c[i];
}

void Frustum::setProj(const CameraSpec& spec) {
	double projtmp[16];
	spec.compute_projection(projtmp);
	memcpy(proj, projtmp, 8*16);

	Map<const RowMatrix4d> frustumProj_(proj);
	Map<RowMatrix4d> frustumProjInv_(projInv);
	frustumProjInv_ = frustumProj_.inverse();
}
void Frustum::setProj(const double* proj__) {
	memcpy(this->proj, proj__, 8*16);

	Map<const RowMatrix4d> frustumProj_(proj);
	Map<RowMatrix4d> frustumProjInv_(projInv);
	frustumProjInv_ = frustumProj_.inverse();
}
void Frustum::setPose(const double* viewInv_) {
	memcpy(viewInv, viewInv_, 16*8);

	Map<const RowMatrix4d> vi(viewInv);
	Map<RowMatrix4d> v(this->view);
	v.topLeftCorner<3,3>() = vi.topLeftCorner<3,3>().transpose();
	v.topRightCorner<3,1>() = -v.topLeftCorner<3,3>()*vi.topRightCorner<3,1>();
	v.row(3) << 0,0,0,1;
}

void Frustum::getCasterMatrix(float* out) {
	Map<const RowMatrix4d> v(this->view);
	Map<const RowMatrix4d> p(this->proj);
	Map<RowMatrix4f> o(out);
	o = (p * v).cast<float>();
}

void Frustum::render(const RenderState& rs) {

	static const float verts[] = {
		-1.f, -1.f, 0.f,
		1.f, -1.f, 0.f,
		1.f, 1.f, 0.f,
		-1.f, 1.f, 0.f,

		-1.f, -1.f, 1.f,
		1.f, -1.f, 1.f,
		1.f, 1.f, 1.f,
		-1.f, 1.f, 1.f
	};
	static const uint16_t inds[] = {
		0,1, 1,2, 2,3, 3,0,
		4+0,4+1, 4+1,4+2, 4+2,4+3, 4+3,4+0,
		0,4, 1,5, 2,6, 3,7
	};


	glUseProgram(0);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3,	GL_FLOAT, 0, verts);
	glColor4fv(color);

	// compute model matrix, which is like a projection matrix.
	if (1) {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);

		const double *mvp = rs.mvp();
		Map<const RowMatrix4d> mvp_(mvp);
		Map<const RowMatrix4d> frustumViewInv_(viewInv);

		// double proj[16];
		// spec.compute_projection(proj);
		// Map<const RowMatrix4d> frustumProjInv_(proj);

		Map<const RowMatrix4d> frustumProj_(projInv);



		float mvpf_column[16];
		Map<Matrix4f> mvpf_column_(mvpf_column);
		mvpf_column_ = (mvp_ * frustumViewInv_ * frustumProj_).cast<float>();

		glLoadMatrixf(mvpf_column);
	}

	glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, inds);

	glDisableClientState(GL_VERTEX_ARRAY);
}

}
