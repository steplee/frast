#pragma once

#include <cassert>
#include <cstring>

#include "window.h"

namespace frast {


struct CameraSpec {
	double w, h;
	// double hfov, vfov;
	double fx_, fy_;
	// These may be set by the camera dynamically.
	double near=1e-7, far=1.3;

	CameraSpec();
	CameraSpec(double w, double h, double vfov);
	CameraSpec(double w, double h, double hfov, double vfov);
	CameraSpec(double w, double h, double hfov, double vfov, double n, double f);
	inline double aspect() const { return h / w; }

	float fx() const;
	float fy() const;
	float hfov() const;
	float vfov() const;

	void compute_projection(double* dest, bool flipY=false) const;
	void compute_ortho(double* dest) const;
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
		inline const CameraSpec& spec() const { return spec_; }

		inline void setSpec(const CameraSpec& spec) {
			spec_ = spec;
			compute_projection();
		}
		virtual void setPosition(double* t) = 0;
		virtual void setRotMatrix(double* R) = 0;

		virtual void step(double dt) = 0;

		inline void compute_projection() {
			spec_.compute_projection(proj_, flipY_);
		}

		virtual bool handleKey(int key, int scancode, int action, int mods) =0;
		virtual bool handleMousePress(int button, int action, int mods) =0;
		virtual bool handleMouseMotion(double x, double y) =0;



	protected:
		CameraSpec spec_;


		alignas(16) double view_[16];
		alignas(16) double viewInv_[16];
		alignas(16) double proj_[16];

	public:
		bool flipY_ = false;
};

struct SimpleMovingCamera : public Camera {
		SimpleMovingCamera(const CameraSpec& spec);
		SimpleMovingCamera();
		inline virtual ~SimpleMovingCamera() {}

	virtual void setPosition(double* t) override;
	virtual void setRotMatrix(double* R) override;
	

	virtual void step(double dt) override;
	// virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) override;
	// virtual void handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) override;
	// virtual void handleMouseMotion(int x, int y, uint8_t mod) override;
	virtual bool handleKey(int key, int scancode, int action, int mods) override;
	virtual bool handleMousePress(int button, int action, int mods) override;
	virtual bool handleMouseMotion(double x, double y) override;

	float minSpeed=1e-8f, speedMult=1;
	double drag_ = 1.9;

	protected:
		void recompute_view();

		alignas(16) double vel_[3] = {0};
		alignas(16) double acc_[3] = {0};
		alignas(16) double quat_[4];
		alignas(16) double dquat_[3] = {0};
		bool leftMouseDown = false, rightMouseDown = false;
		double lastX=0, lastY=0;
};

struct FlatEarthMovingCamera : public Camera {
		FlatEarthMovingCamera(const CameraSpec& spec);
		FlatEarthMovingCamera();
		inline virtual ~FlatEarthMovingCamera() {}

	virtual void setPosition(double* t) override;
	virtual void setRotMatrix(double* R) override;
	

	virtual void step(double dt) override;
	// virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) override;
	// virtual void handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) override;
	// virtual void handleMouseMotion(int x, int y, uint8_t mod) override;
	virtual bool handleKey(int key, int scancode, int action, int mods) override;
	virtual bool handleMousePress(int button, int action, int mods) override;
	virtual bool handleMouseMotion(double x, double y) override;

	float minSpeed=1e-8f, speedMult=1;

	protected:
		void recompute_view();

		double drag_ = 1.9;
		alignas(16) double vel_[3] = {0};
		alignas(16) double acc_[3] = {0};
		alignas(16) double quat_[4];
		alignas(16) double dquat_[3] = {0};
		bool leftMouseDown = false, rightMouseDown = false;
		double lastX=0, lastY=0;
};


struct SphericalEarthMovingCamera : public Camera {
		SphericalEarthMovingCamera(const CameraSpec& spec);
		SphericalEarthMovingCamera();
		inline virtual ~SphericalEarthMovingCamera() {}

	virtual void setPosition(double* t) override;
	virtual void setRotMatrix(double* R) override;
	

	virtual void step(double dt) override;
	// virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) override;
	// virtual void handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) override;
	// virtual void handleMouseMotion(int x, int y, uint8_t mod) override;
	virtual bool handleKey(int key, int scancode, int action, int mods) override;
	virtual bool handleMousePress(int button, int action, int mods) override;
	virtual bool handleMouseMotion(double x, double y) override;

	protected:
		void maybe_set_near_far();
		void recompute_view();

		double drag_ = 2.9;
		alignas(16) double vel_[3] = {0};
		alignas(16) double acc_[3] = {0};
		alignas(16) double quat_[4];
		alignas(16) double dquat_[3] = {0};
		bool leftMouseDown = false, rightMouseDown = false;
		double lastX=0, lastY=0;

		double last_proj_r2 = 0;
};

struct GlobeCamera : public Camera {
		GlobeCamera(const CameraSpec& spec);
		GlobeCamera();
		inline virtual ~GlobeCamera() {}

	virtual void setPosition(double* t) override;
	virtual void setRotMatrix(double* R) override;
	
	virtual void step(double dt) override;
	virtual bool handleKey(int key, int scancode, int action, int mods) override;
	virtual bool handleMousePress(int button, int action, int mods) override;
	virtual bool handleMouseMotion(double x, double y) override;

	// a 4x4 pose matrix
	void setTarget(const double* T);
	void clearTarget();

	protected:
		void maybe_set_near_far();
		void recompute_view();
		void reset_local_zplus(double *quat);

		double pos_[3] = {0};

		double drag_ = 2.9;
		alignas(16) double vel_[3] = {0};
		alignas(16) double acc_[3] = {0};
		alignas(16) double dquat_[3] = {0};
		alignas(16) double freeQuat_[4];
		alignas(16) double targetQuat_[4];
		alignas(16) double quat_[4];

		alignas(16) double targetAcc_[3] = {0};
		alignas(16) double targetDquat_[3] = {0};
		bool leftMouseDown = false, rightMouseDown = false;
		double lastX=0, lastY=0;

		double last_proj_r2 = 0;

		double savedFreePos[3];
		double targetPosOffset[3];
		double targetOriOffset[4];
		std::vector<double> targetInvView;

};


}
