#pragma once

// #include "frastVk/core/window.hpp"
// #include "frastVk/core/window.h"
#include <cassert>
#include <cstring>

#include "window.h"
#include "cameras.h"

namespace frast {


void invertMatrix44(float* out, const float* __restrict__ in);

struct MatrixStack {
	static constexpr int MAX_DEPTH = 4;
	alignas(16) double m[16*MAX_DEPTH];
	int d = 0;

	void push(const double* t, bool increment=true);
	void pop();
	inline double* peek() { assert(d>0); return m+((d-1)*16); }
	inline const double* peek() const { assert(d>0); return m+((d-1)*16); }
	inline void reset() { d=0; }
};


class RenderState {
	public:
		/*
		RenderState(const RenderState&) = delete;
		inline RenderState() : camera(nullptr) {}
		inline RenderState(Camera* cam) : camera(cam) {}
		
		MatrixStack mstack;
		Camera* camera=nullptr;
		// FrameData* frameData = nullptr;

		// Push proj & view matrix.
		// void frameBegin(FrameData* frameData);
		void frameBegin();

		inline const double *view() const { return camera->view(); }
		inline const double *viewInv() const { return camera->viewInv(); }
		inline const double *proj() const { return camera->proj(); }
		inline const double* mvp() const { return mstack.peek(); }
		inline void mvpd(double* d) const {
			const double *m = mstack.peek();
			for (int i=0; i<16; i++) d[i] = m[i];
		}
		inline void mvpf(float* f) const {
			const double *m = mstack.peek();
			for (int i=0; i<16; i++) f[i] = static_cast<float>(m[i]);
		}
		inline void eyed(double* out) const {
			const double* vinv = camera->viewInv();
			out[0] = vinv[0*4+3];
			out[1] = vinv[1*4+3];
			out[2] = vinv[2*4+3];
		}
		*/

		// RenderState(const RenderState&) = delete;
		inline RenderState() : camera(nullptr) {}
		inline RenderState(Camera* cam) : camera(cam) {}

		void frameBegin();

		inline const double* view() const { return camera->view(); }
		inline const double* viewInv() const { return camera->viewInv(); }
		inline const double* modelView() const { return mv_; }
		inline const double* proj() const { return proj_; }
		inline void copyProj(double* out) const { memcpy(out, proj_, sizeof(proj_)); }
		inline void copyModelView(double* out) const { memcpy(out, mv_, sizeof(proj_)); }
		inline void copyProjf(float* out) const { for (int i=0; i<16; i++) out[i] = (float)proj_[i]; }
		inline void copyModelViewf(float* out) const { for (int i=0; i<16; i++) out[i] = (float)mv_[i]; }

		// FIXME: Cache these when they have not changed, as they are called pretty frequently
		void computeMvp(double* out) const;
		void computeMvpf(float* out) const;

		void pushModel(const double* m);
		void pushModel(const float* m);

		inline void copyEye(double* out) const {
			const double* vinv = camera->viewInv();
			out[0] = vinv[0*4+3];
			out[1] = vinv[1*4+3];
			out[2] = vinv[2*4+3];
		}

		Camera* camera=nullptr;

	protected:
		alignas(16) double proj_[16];
		alignas(16) double mv_[16];


};

}
