#pragma once

#include "window.hpp"

class FrameData;

struct CameraSpec {
	double w, h;
	double hfov, vfov;

	CameraSpec();
	CameraSpec(double w, double h, double vfov);
	inline double aspect() const { return h / w; }
};

/*
 * Abstract base class for a camera
 *
 * Subclasses can use the io funcs to move camera.
 * But: view viewInv proj, should always be valid!
 *
 */
struct Camera : public UsesIO {
	public:
		Camera();
		Camera(const CameraSpec& spec);
		inline virtual ~Camera() {}

		inline const double *view() const { return view_; }
		inline const double *proj() const { return proj_; }
		inline const double *viewInv() const { return viewInv_; }
		inline const CameraSpec spec() const { return spec_; }

		inline void setSpec(const CameraSpec& spec) {
			spec_ = spec;
			compute_projection();
		}
		virtual void setPosition(double* t) = 0;
		virtual void setRotMatrix(double* R) = 0;

		virtual void step(double dt) = 0;


	protected:
		CameraSpec spec_;

		void compute_projection();

		alignas(16) double view_[16];
		alignas(16) double viewInv_[16];
		alignas(16) double proj_[16];
};

struct MovingCamera : public Camera {
		MovingCamera(const CameraSpec& spec);
		MovingCamera();
		inline virtual ~MovingCamera() {}

	virtual void setPosition(double* t) override;
	virtual void setRotMatrix(double* R) override;
	

	virtual void step(double dt);
	virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) override;
	virtual void handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) override;
	virtual void handleMouseMotion(int x, int y, uint8_t mod) override;

	protected:
		void recompute_view();

		double drag_ = 1.9;
		alignas(16) double vel_[3] = {0};
		alignas(16) double acc_[3] = {0};
		alignas(16) double quat_[4];
		alignas(16) double dquat_[3] = {0};
		bool mouseDown = false;
		double lastX=0, lastY=0;


};

struct MatrixStack {
	static constexpr int MAX_DEPTH = 7;
	alignas(16) double m[16*MAX_DEPTH];
	int d = 0;

	void push(const double* t);
	void pop();
	inline double* peek() { assert(d>0); return m+((d-1)*16); }
	inline const double* peek() const { assert(d>0); return m+((d-1)*16); }
	inline void reset() { d=0; }
};


class RenderState {
	public:
		RenderState(const RenderState&) = delete;
		inline RenderState() : camera(nullptr) {}
		inline RenderState(std::shared_ptr<Camera> cam) : camera(cam) {}
		
		MatrixStack mstack;
		std::shared_ptr<Camera> camera;
		FrameData* frameData = nullptr;

		// Push proj & view matrix.
		void frameBegin(FrameData* frameData);

		inline const double *view() const { return camera->view(); }
		inline const double *proj() const { return camera->proj(); }
		inline const double* mvp() const { return mstack.peek(); }
		inline void mvpd(double* d) const {
			const double *m = mstack.peek();
			for (int i=0; i<16; i++) d[i] = m[i];
		}
		inline void mvpf(float* f) const {
			const double *m = mstack.peek();
			for (int i=0; i<16; i++) f[i] = (float)m[i];
		}
		inline void eyed(double* out) const {
			const double* vinv = camera->viewInv();
			out[0] = vinv[0*4+3];
			out[1] = vinv[1*4+3];
			out[2] = vinv[2*4+3];
		}

	protected:
};