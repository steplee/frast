
#include "render_state.h"
#include "../utils/eigen.h"

#include <cstring>
#include <cmath>
#include <fmt/core.h>

#include <iostream>


using namespace Eigen;

namespace {
	using RowMatrix3d = Eigen::Matrix<double,3,3,RowMajor>;
	using RowMatrix3f = Eigen::Matrix<float,3,3,RowMajor>;

	inline RowMatrix3d lookAtR(
			Ref<const Vector3d> forward,
			Ref<const Vector3d> up_) {

		Vector3d up = up_;

		Vector3d f = forward.normalized();

		/*
		if (std::abs(f.dot(up)) > .99999) {
			if (std::abs(f.dot(Vector3d::UnitZ())) < .999)
				up = Vector3d::UnitZ();
			else
				up = Vector3d::UnitY();
		}
		*/

		Vector3d r = up.cross(f).normalized();
		Vector3d u = f.cross(r);

		RowMatrix3d R;
		R.col(2) = f;
		R.col(1) = u;
		R.col(0) = r;
		return R;
	}

	inline RowMatrix3d getLtp(Ref<const Vector3d> p) {
		return lookAtR(p, Vector3d::UnitZ());
	}
}

namespace frast {


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

void CameraSpec::compute_projection(double* dest, bool flipY) const {
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
		0, (flipY?-1:1) * -2*1 / 2*v, 0, 0, // negative in gl, pos in vk
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
 *               SimpleMovingCamera
 *
 *
 * =================================================== */


void SimpleMovingCamera::setPosition(double* t) {
	viewInv_[0*4+3] = t[0];
	viewInv_[1*4+3] = t[1];
	viewInv_[2*4+3] = t[2];
	recompute_view();
}
void SimpleMovingCamera::setRotMatrix(double* R) {
	Map<const Matrix<double,3,3,RowMajor>> RR ( R );
	Eigen::Quaterniond q { RR };
	quat_[0] = q.x(); quat_[1] = q.y(); quat_[2] = q.z(); quat_[3] = q.w();
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	viewInv.topLeftCorner<3,3>() = q.toRotationMatrix();

	recompute_view();
}
void SimpleMovingCamera::recompute_view() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );
	view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
	view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
}

// IO.
bool SimpleMovingCamera::handleMousePress(int button, int action, int mods) {
	leftMouseDown = button == GLFW_MOUSE_BUTTON_LEFT and action == GLFW_PRESS;
	rightMouseDown = button == GLFW_MOUSE_BUTTON_RIGHT and action == GLFW_PRESS;
	return false;
}
bool SimpleMovingCamera::handleMouseMotion(double x, double y) {
	Map<Matrix<double,3,1>> dquat { dquat_ };
	if (leftMouseDown and (lastX !=0 or lastY !=0)) {
		dquat(0) += (x - lastX) * .1f;
		dquat(1) += (y - lastY) * .1f;
	}
	lastX = x;
	lastY = y;
	return false;
}
bool SimpleMovingCamera::handleKey(int key, int scancode, int action, int mods) {
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
			std::cout << " [SimpleMovingCamera::step()]\n";
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
void SimpleMovingCamera::step(double dt) {
	Map<Matrix<double,3,1>> vel { vel_ };
	Map<Matrix<double,3,1>> acc { acc_ };
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };

	static int __updates = 0;
	__updates++;

	//constexpr float SPEED = 5.f;
	// double SPEED = .00000000000001f + 5.f * std::fabs(viewInv(2,3));
	double SPEED = minSpeed + speedMult;

	viewInv.topRightCorner<3,1>() += vel * dt + acc * dt * dt * .5 * SPEED;

	vel -= vel * std::min(drag_ * dt, .9999);
	vel += acc * dt * SPEED;
	//if (vel.squaredNorm() < 1e-18) vel.setZero();
	acc.setZero();

		// let dq1 = qexp([this.dy*qspeed,0,0]);
		// let dq2 = qexp([0,this.dx*qspeed,0]);
		// this.q = qmult(dq2, qmult(this.q, dq1));

	/*
	quat = quat * AngleAxisd(-dquat_[0]*dt, Vector3d::UnitY())
				* AngleAxisd( dquat_[1]*dt, Vector3d::UnitX())
				* AngleAxisd(         0*dt, Vector3d::UnitZ());
	*/
	quat = AngleAxisd(-dquat_[0]*dt, Vector3d::UnitX())
		 * quat
		 * AngleAxisd( dquat_[1]*dt, Vector3d::UnitY());
	
	for (int i=0; i<4; i++) quat_[i] = quat.coeffs()(i);
	dquat_[0] = dquat_[1] = dquat_[2] = 0;

	quat = quat.normalized();
	viewInv.topLeftCorner<3,3>() = quat.toRotationMatrix();

	recompute_view();
}

SimpleMovingCamera::SimpleMovingCamera(const CameraSpec& spec) : Camera(spec) {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}
SimpleMovingCamera::SimpleMovingCamera() {
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
	// double SPEED = .00000000000001f + 5.f * std::fabs(viewInv(2,3));
	double SPEED = minSpeed + speedMult * 5.f * std::fabs(viewInv(2,3));

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



/* ===================================================
 *
 *
 *                  GlobeCamera
 *
 *
 * =================================================== */


void GlobeCamera::maybe_set_near_far() {
	double t[3] = { pos_[0], pos_[1], pos_[2] };
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

void GlobeCamera::setPosition(double* t) {
	pos_[0] = t[0];
	pos_[1] = t[1];
	pos_[2] = t[2];
	maybe_set_near_far();
	recompute_view();
}
void GlobeCamera::setRotMatrix(double* R) {

	// NOTE: We assume a global ecef input rotation matrix, so we have to convert to our local form here.

	Map<Vector3d> pos ( pos_ );
	RowMatrix3d Ltp = getLtp(pos);

	Map<const Matrix<double,3,3,RowMajor>> R0 ( R );

	Matrix<double,3,3,RowMajor> RR = Ltp.transpose() * R0;
	// Matrix<double,3,3,RowMajor> RR = R0;


	Eigen::Quaterniond q { RR };
	quat_[0] = q.x(); quat_[1] = q.y(); quat_[2] = q.z(); quat_[3] = q.w();
	freeQuat_[0] = q.x(); freeQuat_[1] = q.y(); freeQuat_[2] = q.z(); freeQuat_[3] = q.w();
	targetQuat_[0] = q.x(); targetQuat_[1] = q.y(); targetQuat_[2] = q.z(); targetQuat_[3] = q.w();
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	viewInv.topLeftCorner<3,3>() = q.toRotationMatrix();

	recompute_view();
}
void GlobeCamera::recompute_view() {

	Map<Vector3d> pos__ ( pos_ );
	Map<Quaterniond> quat ( quat_ );
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );
	Map<Vector3d> targetPosOffset_ (targetPosOffset);

	Vector3d pos = pos__;

	if (0 != targetInvView.size()) {
		pos += viewInv.topLeftCorner<3,3>() * targetPosOffset_;
	}

	RowMatrix3d Ltp = getLtp(pos);

	viewInv.topRightCorner<3,1>() = pos;
	// viewInv.topLeftCorner<3,3>() = Ltp * quat.toRotationMatrix();
	viewInv.topLeftCorner<3,3>() = quat.toRotationMatrix();
	viewInv.row(3) << 0,0,0,1;

	view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
	view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
}

// IO
bool GlobeCamera::handleMousePress(int button, int action, int mods) {
	leftMouseDown = button == GLFW_MOUSE_BUTTON_LEFT and action == GLFW_PRESS;
	rightMouseDown = button == GLFW_MOUSE_BUTTON_RIGHT and action == GLFW_PRESS;
	return false;
}
bool GlobeCamera::handleMouseMotion(double x, double y) {
	Map<Matrix<double,3,1>> dquat { dquat_ };
	if (leftMouseDown and (lastX !=0 or lastY !=0)) {
		dquat(0) += (x - lastX) * .1f;
		dquat(1) += (y - lastY) * .1f;
	}
	lastX = x;
	lastY = y;
	return false;
}
bool GlobeCamera::handleKey(int key, int scancode, int action, int mods) {

	bool isDown = action == GLFW_PRESS or action == GLFW_REPEAT;
	if (isDown) {
		Map<Matrix<double,3,1>> acc { acc_ };
		Map<Matrix<double,3,1>> targetAcc { targetAcc_ };
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
			std::cout << " [GlobeCamera::step()]\n";
			std::cout << "      - vel " << vel.transpose() << "\n";
			std::cout << "      - acc " << acc.transpose() << "\n";
			std::cout << "      - pos " << viewInv.topRightCorner<3,1>().transpose() << "\n";
			std::cout << "      - z+  " << viewInv.block<3,1>(0,2).transpose() << "\n";
			std::cout << "      - q   " << quat.coeffs().transpose() << "\n";
			std::cout << "      - n/f " << spec().near << " " << spec().far << "\n";
		}

		// Make accel happen local to frame
		// acc += viewInv.topLeftCorner<3,3>() * dacc;
		if (0 == targetInvView.size())
			acc += dacc;
		else
			targetAcc += dacc;
	}
	return false;
}

void GlobeCamera::reset_local_zplus(double *theQuat) {

	// FIXME:
	// The camera controls are locally consistent: the camera never rolls.
	// But it may roll due to precision after moving a lot.
	// We want any the R(2,0) component to be zero.
	// How do I achieve this constraint?
	// My idea was to just reform the matrix based on looking at the current Z+ and with up as LTP-Z+.
	// Did not work.

	Map<Quaterniond> quat ( theQuat );

	Map<Vector3d> pos  ( pos_ );
	Map<const Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	RowMatrix3d R = viewInv.topLeftCorner<3,3>();

	RowMatrix3d ltp = getLtp(pos);
	RowMatrix3d localR = ltp.transpose() * R;
	// RowMatrix3d localR = R;


	if (std::abs(localR(2,0)) > 4e-6 and std::abs(localR(2,2)) < .99) {
		// std::cout << " - running reset XZ on matrix:\n" << localR << "\n";
		quat = lookAtR(
				(localR).col(2),
				// Vector3d::UnitZ());
				-Vector3d::UnitZ());

		recompute_view();
	}


}

// FIXME: This code is shit.
void GlobeCamera::step(double dt) {
	// fmt::print(" - [GlobeCamera::step] dt {}\n", dt);
	using namespace Eigen;
	Map<Matrix<double,3,1>> vel { vel_ };
	Map<Matrix<double,3,1>> acc { acc_ };
	Map<Matrix<double,3,1>> targetAcc { targetAcc_ };
	Map<Matrix<double,3,1>> targetDquat { targetDquat_ };
	Map<Vector3d> targetPosOffset_ (targetPosOffset);
	Map<Quaterniond> targetOriOffset_ (targetOriOffset);
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );

	Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };

	// Quaterniond dquat1{Quaterniond::Identity()};
	AngleAxisd dquatFollow = AngleAxisd{0, Vector3d::UnitX()};

	Map<Vector3d> pos { pos_ };
	RowMatrix3d Ltp = getLtp(pos);

	static int __updates = 0;
	__updates++;

	double r = viewInv.topRightCorner<3,1>().norm();
	// double d = r - 1.0;
	double d = r - .9966;

	drag_ = 15.;
	double SPEED = .0000000001 + 25.f * std::max(d, 3e-4);


	Quaterniond rotLeftSide = Quaterniond(AngleAxisd(dquat_[0]*dt, Vector3d::UnitZ()));
	Quaterniond rotRghtSide = Quaterniond(AngleAxisd(dquat_[1]*dt, Vector3d::UnitX()));
	Vector3d err = Vector3d::Zero();

	if (targetInvView.size() == 0) {
		quat = Quaterniond { freeQuat_[3], freeQuat_[0], freeQuat_[1], freeQuat_[2] };

		quat = rotLeftSide * quat * rotRghtSide;
		// quat = rotLeftSide * rotRghtSide * quat;
		// quat = quat * rotLeftSide * rotRghtSide;
		
		for (int i=0; i<4; i++) freeQuat_[i] = quat.coeffs()(i);
		quat = Ltp * quat;

	} else {
		quat = Quaterniond { targetQuat_[3], targetQuat_[0], targetQuat_[1], targetQuat_[2] };

		// quat = rotLeftSide * quat;// * rotRghtSide;

		Map<Matrix<double,4,4,RowMajor>> T(targetInvView.data());
		Vector3d tp = T.topRightCorner<3,1>();
		Matrix3d TR = T.topLeftCorner<3,3>();

		Vector3d tp2 = tp;// + targetPosOffset_;
		err = (tp2 - pos);
		acc.setZero();
		acc += 95 * viewInv.topLeftCorner<3,3>().transpose() * err / SPEED;

		Quaterniond quatOff { targetOriOffset[3], targetOriOffset[0], targetOriOffset[1], targetOriOffset[2] };
		quatOff = rotLeftSide * quatOff * rotRghtSide;
		for (int i=0; i<4; i++) targetOriOffset[i] = quatOff.coeffs()(i);

		Quaterniond a = Quaterniond{Ltp.transpose() * TR};
		dquatFollow = AngleAxisd { (quat).inverse() * a };
		dquatFollow.angle() *= .9 * dt;


		Quaterniond quatSaved = quat * Quaterniond{dquatFollow};
		quat = Ltp * quatSaved * quatOff;
		for (int i=0; i<4; i++) targetQuat_[i] = quatSaved.coeffs()(i);
	}


	// vel.setZero();
	vel -= vel * (drag_ * dt);
	if (vel.squaredNorm() < 1e-18) vel.setZero();

	if (0 == targetInvView.size()) {
		vel += acc * dt * SPEED;
		pos += viewInv.topLeftCorner<3,3>() * (vel * dt + acc * dt * dt * .5 * SPEED);
	} else {
		vel += acc * dt * SPEED;
		// targetPosOffset_ += viewInv.topLeftCorner<3,3>() * (targetAcc * dt * .1 * .5 * SPEED);
		targetPosOffset_ += (targetAcc * dt * .1 * .5 * SPEED);
		pos += viewInv.topLeftCorner<3,3>() * (vel * dt + acc * dt * dt * .5 * SPEED);
		// pos += err * dt;
		targetAcc.setZero();
	}
	acc.setZero();

	quat = quat.normalized();
	for (int i=0; i<4; i++) quat_[i] = quat.coeffs()(i);
	dquat_[0] = dquat_[1] = dquat_[2] = 0;


	maybe_set_near_far();
	recompute_view();
	if (targetInvView.size() == 0) {
		reset_local_zplus(freeQuat_);
	} else {
		Quaterniond tmp = targetOriOffset_;
		// reset_local_zplus(tmp.coeffs().data());
		// tmp =  tmp;

		Map<const Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
		RowMatrix3d RR = tmp.toRotationMatrix();

		if (std::abs(RR(2,0)) > 4e-16) {
			// fmt::print(" - reset targetOriOffset roll.\n");
			// std::cout << " RR :\n" << RR << "\n";
			tmp = lookAtR(
					(RR).col(2),
					Vector3d::UnitZ());
					// -Vector3d::UnitZ());
		}

		// targetOriOffset_ = tmp;

	}
	// reset_local_zplus(targetInvView.size() == 0 ? freeQuat_ : targetOriOffset);
}

void GlobeCamera::setTarget(const double* T) {
	if (targetInvView.size() == 0) for (int i=0; i<3; i++) savedFreePos[i] = pos_[i];
	if (targetInvView.size() == 0) targetInvView.resize(16);
	memcpy(targetInvView.data(), T, 8*16);
}
void GlobeCamera::clearTarget() {
	targetInvView.clear();
	for (int i=0; i<3; i++) pos_[i] = savedFreePos[i];
}

GlobeCamera::GlobeCamera(const CameraSpec& spec) : Camera(spec) {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	quat_[3] = sqrt(2.) / 2.;
	quat_[1] = sqrt(2.) / 2.;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();

	Map<Vector3d> targetPosOffset_ (targetPosOffset);
	Map<Vector4d> targetOriOffset_ (targetOriOffset);
	targetPosOffset_ << 0,0, -1000/6e6;
	targetOriOffset_ << 0,0,0,1;
}

GlobeCamera::GlobeCamera() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	quat_[3] = sqrt(2.) / 2.;
	quat_[1] = sqrt(2.) / 2.;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();

	Map<Vector3d> targetPosOffset_ (targetPosOffset);
	Map<Vector4d> targetOriOffset_ (targetOriOffset);
	targetPosOffset_ << 0,0, 100;
	targetOriOffset_ << 0,0,0,1;
}




}
