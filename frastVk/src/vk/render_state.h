#pragma once

#include "window.hpp"

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

		inline double *view() { return view_; }
		inline double *proj() { return proj_; }
		inline double *viewInv() { return viewInv_; }
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
	static constexpr int MAX_DEPTH = 5;
	alignas(16) double m[16*5];
	int d = 0;

	void push(double* t);
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

		// Push proj & view matrix.
		void frameBegin();

		inline double *view() { return camera->view(); }
		inline double *proj() { return camera->proj(); }
		inline double* mvp() { return mstack.peek(); }
		inline void mvpd(double* d) {
			double *m = mstack.peek();
			for (int i=0; i<16; i++) m[i] = d[i];
		}
		inline void mvpf(float* f) {
			double *m = mstack.peek();
			for (int i=0; i<16; i++) f[i] = (float)m[i];
		}

	protected:
};
