#include "frustum.h"

#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/core/shader.h"
#include "frast2/frastgl/shaders/earth.h"
#include "frast2/frastgl/utils/eigen.h"
#include "frast2/frastgl/utils/check.hpp"

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

	trailVbo=0;
	glGenBuffers(1, &trailVbo);
	glCheck(glBindBuffer(GL_ARRAY_BUFFER, trailVbo));
	glCheck(glBufferStorage(GL_ARRAY_BUFFER, trailSize, nullptr, GL_MAP_READ_BIT|GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT));
	trailData = (float*) glMapBufferRange(GL_ARRAY_BUFFER, 0, trailSize, GL_MAP_FLUSH_EXPLICIT_BIT|GL_MAP_WRITE_BIT|GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT);
	glCheck("after mapBufferRange");
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	trailIndex = 0;

}

Frustum::~Frustum() {
	if (trailVbo) {
		if (trailData) {
			glBindBuffer(GL_ARRAY_BUFFER, trailVbo);
			glUnmapBuffer(GL_ARRAY_BUFFER);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glDeleteBuffers(1, &trailVbo);
	}
}

void Frustum::resetTrail() { trailIndex = 0; haveWaitingNextPos = false; }

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
void Frustum::setPose(const double* viewInv_, bool pushTrail) {
	memcpy(viewInv, viewInv_, 16*8);

	Map<const RowMatrix4d> vi(viewInv);
	Map<RowMatrix4d> v(this->view);
	v.topLeftCorner<3,3>() = vi.topLeftCorner<3,3>().transpose();
	v.topRightCorner<3,1>() = -v.topLeftCorner<3,3>()*vi.topRightCorner<3,1>();
	v.row(3) << 0,0,0,1;

	if (pushTrail) {
		haveWaitingNextPos = true;
		waitingNextPos[0] = viewInv[4*0+3];
		waitingNextPos[1] = viewInv[4*1+3];
		waitingNextPos[2] = viewInv[4*2+3];

	}

}

void Frustum::maybeUpdateTrail() {
	if (!haveWaitingNextPos) return;
	haveWaitingNextPos = false;

	// FIXME: Lock a spinlock.
	float p[3];
	{
		p[0] = waitingNextPos[0];
		p[1] = waitingNextPos[1];
		p[2] = waitingNextPos[2];
	}

	// Decimate the trail.
	if (trailIndex == trailLength) {
		float *tmp = (float*) malloc(trailSize/2);
		for (int i=0; i<trailLength/2; i++) {
			// Box filter
			int j = i * 2;
			int k = i * 2 + (i>0);
			tmp[i*3+0] = (trailData[j*3+0] + trailData[k*3+0]) * .5f;
			tmp[i*3+1] = (trailData[j*3+1] + trailData[k*3+1]) * .5f;
			tmp[i*3+2] = (trailData[j*3+2] + trailData[k*3+2]) * .5f;
		}
		memcpy(trailData, tmp, trailSize/2);
		free(tmp);
		trailIndex = trailIndex / 2;
	}

	trailData[trailIndex*3+0] = p[0];
	trailData[trailIndex*3+1] = p[1];
	trailData[trailIndex*3+2] = p[2];
	trailIndex++;
	if (trailIndex > 1) {
		glBindBuffer(GL_ARRAY_BUFFER, trailVbo);
		glCheck(glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, (trailIndex-1)*3*4)); // FIXME: Only do used range.
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

void Frustum::getCasterMatrix(float* out) const {
	Map<const RowMatrix4d> v(this->view);
	Map<const RowMatrix4d> p(this->proj);
	Map<RowMatrix4f> o(out);
	o = (p * v).cast<float>();
}
void Frustum::getModelMatrix(double* out) const {
	Map<const RowMatrix4d> v(this->view);
	Map<RowMatrix4d> o(out);
	o = v.inverse();
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

	RowMatrix4d mvp;
	rs.computeMvp(mvp.data());

	// compute model matrix, which is like a projection matrix.
	if (1) {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);

		// const double *mvp = rs.mvp();
		// Map<const RowMatrix4d> mvp_(mvp);
		Map<const RowMatrix4d> frustumViewInv_(viewInv);

		// double proj[16];
		// spec.compute_projection(proj);
		// Map<const RowMatrix4d> frustumProjInv_(proj);

		Map<const RowMatrix4d> frustumProj_(projInv);

		float mvpf_column[16];
		Map<Matrix4f> mvpf_column_(mvpf_column);
		mvpf_column_ = (mvp * frustumViewInv_ * frustumProj_).cast<float>();

		glLoadMatrixf(mvpf_column);
	}

	glCheck(glDrawElements(GL_LINES, 24, GL_UNSIGNED_SHORT, inds));


	maybeUpdateTrail();

	if (trailIndex > 1) {
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		RowMatrix4f mvpf_column = (mvp).transpose().cast<float>();
		glLoadMatrixf(mvpf_column.data());

		glUseProgram(0);
		(glBindBuffer(GL_ARRAY_BUFFER, trailVbo));
		(glVertexPointer(3,	GL_FLOAT, 0, 0));
		(glDrawArrays(GL_LINE_STRIP, 0, trailIndex));
		(glBindBuffer(GL_ARRAY_BUFFER, 0));
	}
	glDisableClientState(GL_VERTEX_ARRAY);



	if (ellps != nullptr) {
		ellps->render(rs);
	}
}

Ellipsoid* Frustum::getOrCreateEllipsoid() {
	if (ellps == nullptr) ellps = std::make_unique<Ellipsoid>();
	return ellps.get();

}

}
