#include "render_state.h"
#include "../utils/eigen.h"

#include <cstring>
#include <cmath>
#include <fmt/core.h>

#include <iostream>


using namespace Eigen;

namespace frast {

void invertMatrix44(float* out, const float* __restrict__ in) {
	Map<const Matrix<float,4,4,RowMajor>> I ( in );
	Map<      Matrix<float,4,4,RowMajor>> O ( out );
	O = I.inverse();
}

/* ===================================================
 *
 *
 *                  MatrixStack
 *
 *
 * =================================================== */

void MatrixStack::push(const double* t) {
	if (d == 0) {
		memcpy(m, t, sizeof(double)*16);
		Map<      Matrix<double,4,4,RowMajor>> C ( m+(d)*16 );
		d++;
		//printf(" - push matrix to depth %d .. done\n", d); fflush(stdout);
		//std::cout << C << std::endl;
	} else {
		assert(d < (MAX_DEPTH-1)); // too deep
		// Note: requires alignment of input!
		Map<const Matrix<double,4,4,RowMajor>> A ( m+(d-1)*16 );
		Map<const Matrix<double,4,4,RowMajor>> B ( t );
		Map<      Matrix<double,4,4,RowMajor>> C ( m+(d++)*16 );
		C = A * B;
		//printf(" - push matrix to depth %d .. done\n", d); fflush(stdout);
		//std::cout << B << std::endl;
		//std::cout << C << std::endl;
	}
}
void MatrixStack::pop() {
	assert(d > 0);
	d--;
}


/* ===================================================
 *
 *
 *                  RenderState
 *
 *
 * =================================================== */


/*void RenderState::frameBegin(FrameData* d) {
	frameData = d;
	mstack.reset();
	mstack.push(camera->proj());
	mstack.push(camera->view());
}*/
void RenderState::frameBegin() {
	assert(camera);
	mstack.reset();
	mstack.push(camera->proj());
	mstack.push(camera->view());
}


/* ===================================================
 *
 *
 *            Camera / CameraSpec
 *
 *
 * =================================================== */


CameraSpec::CameraSpec() {
	w=h=0;
	fx_=fy_=0;
}
CameraSpec::CameraSpec(double w, double h, double vfov) : w(w), h(h) {
	// double hfov = std::atan(std::tan(vfov) / aspect());
	// double hfov = vfov / aspect();
	fx_ = w / (2 * std::tan(vfov / 2.)) * (h/w);
	// fx_ = w / (2 * std::tan(vfov / 2.));
	fy_ = h / (2 * std::tan(vfov / 2.));
}
CameraSpec::CameraSpec(double w, double h, double hfov, double vfov) : w(w), h(h) {
	fx_ = w / (2 * std::tan(hfov / 2.));
	fy_ = h / (2 * std::tan(vfov / 2.));
}
CameraSpec::CameraSpec(double w, double h, double hfov, double vfov, double n, double f) : w(w), h(h), near(n), far(f) {
	fx_ = w / (2 * std::tan(hfov / 2.));
	fy_ = h / (2 * std::tan(vfov / 2.));
}

float CameraSpec::fx() const {
	return fx_;
}
float CameraSpec::fy() const {
	return fy_;
}
float CameraSpec::hfov() const {
	return 2 * std::atan(w / (2 * fx_));
}
float CameraSpec::vfov() const {
	return 2 * std::atan(h / (2 * fy_));
}

void CameraSpec::compute_projection(double* dest) const {
	// compute_ortho(dest); return;

	Map<Matrix<double,4,4,RowMajor>> proj ( dest );
	// double u = aspect() / std::tan(.5* vfov);
	// double u = fx_ * 2. / (w /  aspect());
	double u = fx_ * 2. / (w);
	double v = fy_ * 2. / h;
	// double u = w / (.5 * fx_);
	// double v = h / (.5 * fy_);
	double n = near, f = far;
	proj <<
		//2*n / 2*u, 0, 0, 0,
		//0, 2*n / 2*v, 0, 0,
		2*1 / 2*u, 0, 0, 0,
		0, -2*1 / 2*v, 0, 0, // negative in gl, pos in vk
		0, 0, (f+n)/(f-n), -2*f*n/(f-n),
		0, 0, 1, 0;
}

void CameraSpec::compute_ortho(double* dest) const {
	Map<Matrix<double,4,4,RowMajor>> proj ( dest );
	// double u = aspect() / std::tan(.5* vfov);
	// double u = fx_ * 2. / (w /  aspect());
	double u = fx_ * 1. / (w);
	double v = fy_ * 1. / h;
	// u=1/u;
	// v=1/v;
	// double u = w / (.5 * fx_);
	// double v = h / (.5 * fy_);
	double n = near, f = far;
	proj <<
		2*1 / 2*u, 0, 0, 0,
		0, -2*1 / 2*v, 0, 0, // negative in gl, pos in vk
		// 0, 0, (f+n)/(f-n), -2*f*n/(f-n),
		0, 0, -2/(f-n),  (f+n)/(f-n),
		0, 0, 0, 1;

}

Camera::Camera(const CameraSpec& spec) {
	setSpec(spec);
}
Camera::Camera() {
	CameraSpec spec{1,1, 40. * M_PI / 180.};
	setSpec(spec);

}



/* ===================================================
 *
 *
 *               FlatEarthMovingCamera
 *
 *
 * =================================================== */


void FlatEarthMovingCamera::setPosition(double* t) {
	viewInv_[0*4+3] = t[0];
	viewInv_[1*4+3] = t[1];
	viewInv_[2*4+3] = t[2];
	recompute_view();
}
void FlatEarthMovingCamera::setRotMatrix(double* R) {
	Map<const Matrix<double,3,3,RowMajor>> RR ( R );
	Eigen::Quaterniond q { RR };
	quat_[0] = q.x(); quat_[1] = q.y(); quat_[2] = q.z(); quat_[3] = q.w();
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	viewInv.topLeftCorner<3,3>() = q.toRotationMatrix();

	recompute_view();
}
void FlatEarthMovingCamera::recompute_view() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );
	view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
	view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
}

// IO.
bool FlatEarthMovingCamera::handleMousePress(int button, int action, int mods) {
	leftMouseDown = button == GLFW_MOUSE_BUTTON_LEFT and action == GLFW_PRESS;
	rightMouseDown = button == GLFW_MOUSE_BUTTON_RIGHT and action == GLFW_PRESS;
	return false;
}
bool FlatEarthMovingCamera::handleMouseMotion(double x, double y) {
	Map<Matrix<double,3,1>> dquat { dquat_ };
	if (leftMouseDown and (lastX !=0 or lastY !=0)) {
		dquat(0) += (x - lastX) * .1f;
		dquat(1) += (y - lastY) * .1f;
	}
	lastX = x;
	lastY = y;
	return false;
}
bool FlatEarthMovingCamera::handleKey(int key, int scancode, int action, int mods) {
	bool isDown = action == GLFW_PRESS or action == GLFW_REPEAT;
	if (isDown or action==GLFW_REPEAT) {
		Map<Matrix<double,3,1>> acc { acc_ };
		Map<const Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );

		Vector3d dacc { Vector3d::Zero() };
		if (isDown and key == GLFW_KEY_A) dacc(0) += -1;
		if (isDown and key == GLFW_KEY_D) dacc(0) +=  1;
		if (isDown and key == GLFW_KEY_W) dacc(2) +=  1;
		if (isDown and key == GLFW_KEY_S) dacc(2) += -1;
		if (isDown and key == GLFW_KEY_F) dacc(1) +=  1;
		if (isDown and key == GLFW_KEY_E) dacc(1) += -1;

		if (isDown and key == GLFW_KEY_I) {
			Map<Matrix<double,3,1>> vel { vel_ };
			Map<Matrix<double,3,1>> acc { acc_ };
			Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
			Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };
			std::cout << " [FlatEarthMovingCamera::step()]\n";
			std::cout << "      - vel " << vel.transpose() << "\n";
			std::cout << "      - acc " << acc.transpose() << "\n";
			std::cout << "      - pos " << viewInv.topRightCorner<3,1>().transpose() << "\n";
			std::cout << "      - z+  " << viewInv.block<3,1>(0,2).transpose() << "\n";
			std::cout << "      - q   " << quat.coeffs().transpose() << "\n";
		}

		// Make accel happen local to frame
		acc += viewInv.topLeftCorner<3,3>() * dacc;
	}
	return false;
}
void FlatEarthMovingCamera::step(double dt) {
	Map<Matrix<double,3,1>> vel { vel_ };
	Map<Matrix<double,3,1>> acc { acc_ };
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };

	static int __updates = 0;
	__updates++;

	//constexpr float SPEED = 5.f;
	double SPEED = .00000000000001f + 5.f * std::fabs(viewInv(2,3));

	viewInv.topRightCorner<3,1>() += vel * dt + acc * dt * dt * .5 * SPEED;

	vel += acc * dt * SPEED;
	vel -= vel * (drag_ * dt);
	//if (vel.squaredNorm() < 1e-18) vel.setZero();
	acc.setZero();

	quat = quat * AngleAxisd(-dquat_[0]*dt, Vector3d::UnitY())
				* AngleAxisd( dquat_[1]*dt, Vector3d::UnitX())
				* AngleAxisd(         0*dt, Vector3d::UnitZ());
	for (int i=0; i<4; i++) quat_[i] = quat.coeffs()(i);
	dquat_[0] = dquat_[1] = dquat_[2] = 0;

	quat = quat.normalized();
	viewInv.topLeftCorner<3,3>() = quat.toRotationMatrix();

	recompute_view();
}

FlatEarthMovingCamera::FlatEarthMovingCamera(const CameraSpec& spec) : Camera(spec) {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}
FlatEarthMovingCamera::FlatEarthMovingCamera() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}


/* ===================================================
 *
 *
 *                  SphericalEarthMovingCamera
 *
 *
 * =================================================== */

void SphericalEarthMovingCamera::maybe_set_near_far() {
	double t[3] = { viewInv_[0*4+3], viewInv_[1*4+3], viewInv_[2*4+3] };
	double r2 = (t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
	// TODO: Make sure this is okay. Just sort of winged it and it seems okay.
	if (std::abs(last_proj_r2 - r2) > std::abs(last_proj_r2 - 1) / 10.) {
		last_proj_r2 = r2;
		double d = std::sqrt(r2) - 1; // Don't use .9966 here: we'd rather overestimate the distance!
		spec_.near = std::max(3e-6, d / 10);
		spec_.far = std::max(.1, (d+1e-2) * 5);
		compute_projection();
		// fmt::print(" - [SECam] set near far to {} {}\n", spec_.near, spec_.far);
	} else {
		// fmt::print(" - [SECam] not touching near far: {} {}\n", last_proj_r2, r2);
	}
}

void SphericalEarthMovingCamera::setPosition(double* t) {
	viewInv_[0*4+3] = t[0];
	viewInv_[1*4+3] = t[1];
	viewInv_[2*4+3] = t[2];
	maybe_set_near_far();
	recompute_view();
}
void SphericalEarthMovingCamera::setRotMatrix(double* R) {
	Map<const Matrix<double,3,3,RowMajor>> RR ( R );
	Eigen::Quaterniond q { RR };
	quat_[0] = q.x(); quat_[1] = q.y(); quat_[2] = q.z(); quat_[3] = q.w();
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	viewInv.topLeftCorner<3,3>() = q.toRotationMatrix();

	recompute_view();
}
void SphericalEarthMovingCamera::recompute_view() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );
	view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
	view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
}

// IO
bool SphericalEarthMovingCamera::handleMousePress(int button, int action, int mods) {
	leftMouseDown = button == GLFW_MOUSE_BUTTON_LEFT and action == GLFW_PRESS;
	rightMouseDown = button == GLFW_MOUSE_BUTTON_RIGHT and action == GLFW_PRESS;
	return false;
}
bool SphericalEarthMovingCamera::handleMouseMotion(double x, double y) {
	Map<Matrix<double,3,1>> dquat { dquat_ };
	if (leftMouseDown and (lastX !=0 or lastY !=0)) {
		dquat(0) += (x - lastX) * .1f;
		dquat(1) += (y - lastY) * .1f;
	}
	lastX = x;
	lastY = y;
	return false;
}
bool SphericalEarthMovingCamera::handleKey(int key, int scancode, int action, int mods) {

	bool isDown = action == GLFW_PRESS or action == GLFW_REPEAT;
	if (isDown) {
		Map<Matrix<double,3,1>> acc { acc_ };
		Map<const Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );

		Vector3d dacc { Vector3d::Zero() };
		if (isDown and key == GLFW_KEY_A) dacc(0) += -1;
		if (isDown and key == GLFW_KEY_D) dacc(0) +=  1;
		if (isDown and key == GLFW_KEY_W) dacc(2) +=  1;
		if (isDown and key == GLFW_KEY_S) dacc(2) += -1;
		if (isDown and key == GLFW_KEY_F) dacc(1) +=  1;
		if (isDown and key == GLFW_KEY_E) dacc(1) += -1;

		if (isDown and key == GLFW_KEY_I) {
			Map<Matrix<double,3,1>> vel { vel_ };
			Map<Matrix<double,3,1>> acc { acc_ };
			Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
			Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };
			std::cout << " [SphericalEarthMovingCamera::step()]\n";
			std::cout << "      - vel " << vel.transpose() << "\n";
			std::cout << "      - acc " << acc.transpose() << "\n";
			std::cout << "      - pos " << viewInv.topRightCorner<3,1>().transpose() << "\n";
			std::cout << "      - z+  " << viewInv.block<3,1>(0,2).transpose() << "\n";
			std::cout << "      - q   " << quat.coeffs().transpose() << "\n";
			std::cout << "      - n/f " << spec().near << " " << spec().far << "\n";
		}

		// Make accel happen local to frame
		acc += viewInv.topLeftCorner<3,3>() * dacc;
	}
	return false;
}
void SphericalEarthMovingCamera::step(double dt) {
	// fmt::print(" - [SphericalEarthMovingCamera::step] dt {}\n", dt);
	Map<Matrix<double,3,1>> vel { vel_ };
	Map<Matrix<double,3,1>> acc { acc_ };
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };

	static int __updates = 0;
	__updates++;

	double r = viewInv.topRightCorner<3,1>().norm();
	// double d = r - 1.0;
	double d = r - .9966;

	//constexpr float SPEED = 5.f;
	// double SPEED = .00000000000001f + 5.f * std::fabs(viewInv(2,3));
	drag_ = 15.;
	double SPEED = .0000000001 + 25.f * std::max(d, 3e-4);

	viewInv.topRightCorner<3,1>() += vel * dt + acc * dt * dt * .5 * SPEED;

	vel += acc * dt * SPEED;
	vel -= vel * (drag_ * dt);
	//if (vel.squaredNorm() < 1e-18) vel.setZero();
	acc.setZero();

	Vector3d pos = viewInv.topRightCorner<3,1>();
	Vector3d n = pos.normalized();
	quat = AngleAxisd(dquat_[0]*dt, n)
				// * AngleAxisd( dquat_[1]*dt, Vector3d::UnitZ().cross(n).normalized())
				// * AngleAxisd( dquat_[1]*dt, Vector3d::UnitZ().cross(n).normalized())
				// * AngleAxisd( dquat_[1]*dt, viewInv.block<1,3>(2,0).normalized())
				* AngleAxisd(         0*dt, Vector3d::UnitZ()) * quat
				* AngleAxisd( dquat_[1]*dt, Vector3d::UnitX());
	/*
	quat = quat * AngleAxisd(-dquat_[0]*dt, Vector3d::UnitY())
				* AngleAxisd( dquat_[1]*dt, Vector3d::UnitX())
				* AngleAxisd(         0*dt, Vector3d::UnitZ());
				*/

	// Make X axis normal to world
	if (pos.norm() < 1.2) {
		float speed = std::min(std::max(.1f - ((float)pos.norm() - 1.2f), 0.f), .1f);
		auto R = quat.toRotationMatrix();
		double angle = R.col(0).dot(n);
		quat = quat * AngleAxisd(angle*speed, n.normalized());
	}


	for (int i=0; i<4; i++) quat_[i] = quat.coeffs()(i);
	dquat_[0] = dquat_[1] = dquat_[2] = 0;

	quat = quat.normalized();
	viewInv.topLeftCorner<3,3>() = quat.toRotationMatrix();

	maybe_set_near_far();
	recompute_view();
}

SphericalEarthMovingCamera::SphericalEarthMovingCamera(const CameraSpec& spec) : Camera(spec) {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}
SphericalEarthMovingCamera::SphericalEarthMovingCamera() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}


}
