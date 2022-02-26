#include "render_state.h"

#include <cstring>
#include <cmath>

#include <iostream>
#include <Eigen/Core>
using namespace Eigen;

void MatrixStack::push(double* t) {
	if (d == 0) {
		memcpy(m, t, sizeof(double)*16);
		Map<      Matrix<double,4,4,RowMajor>> C ( m+(d)*16 );
		d++;
		//printf(" - push matrix to depth %d .. done\n", d); fflush(stdout);
		//std::cout << C << std::endl;
	} else {
		assert(d < (MAX_DEPTH-1)); // too deep
		printf(" - push matrix to depth %d\n", d); fflush(stdout);
		/*
		for (int i=0; i<4; i++)
		for (int j=0; j<4; j++) {
			double acc = 0;
			for (int k=0; k<4; k++) acc += m[d*16+4*i+k] * t[k*4+j];
			m[(d+1)*16 + 4*i + j] = acc;
		}
		d++;
		*/

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




CameraSpec::CameraSpec() {
	w=h=0;
	hfov=vfov=0;
}
CameraSpec::CameraSpec(double w, double h, double vfov) : w(w), h(h), vfov(vfov) {
	hfov = std::atan(std::tan(vfov) / aspect());
}


Camera::Camera(const CameraSpec& spec) {
	setSpec(spec);
}
Camera::Camera() {
	CameraSpec spec{1,1, 40. * M_PI / 180.};
	setSpec(spec);

}

void Camera::compute_projection() {
	Map<Matrix<double,4,4,RowMajor>> proj ( proj_ );
	//double u = 1.f / std::tan(.5* spec_.hfov);
	double u = spec_.aspect() / std::tan(.5* spec_.vfov);
	double v = 1.f / std::tan(.5* spec_.vfov);
	//double n = .02f, f =  200.f;
	//double n = .02f, f =  200.f;
	double n = .01 * 2.38418579e-7, f =  .4f;
	proj <<
		//2*n / 2*u, 0, 0, 0,
		//0, 2*n / 2*v, 0, 0,
		2*1 / 2*u, 0, 0, 0,
		0, 2*1 / 2*v, 0, 0,
		0, 0, (f+n)/(f-n), -2*f*n/(f-n),
		0, 0, 1, 0;

}

void MovingCamera::setPosition(double* t) {
	viewInv_[0*4+3] = t[0];
	viewInv_[1*4+3] = t[1];
	viewInv_[2*4+3] = t[2];
	recompute_view();
}
void MovingCamera::setRotMatrix(double* R) {
	/*
	for (int i=0; i<3; i++)
	for (int j=0; j<3; j++)
		viewInv_[i*4+j] = R[i*3+j];
	*/
	Map<const Matrix<double,3,3,RowMajor>> RR ( R );
	Eigen::Quaterniond q { RR };
	//quat_[0] = q.w(); quat_[1] = q.x(); quat_[2] = q.y(); quat_[3] = q.z();
	quat_[0] = q.x(); quat_[1] = q.y(); quat_[2] = q.z(); quat_[3] = q.w();
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	viewInv.topLeftCorner<3,3>() = q.toRotationMatrix();

	recompute_view();
}
void MovingCamera::recompute_view() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );
	view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
	view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
}

void MovingCamera::handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) {
	mouseDown = isPressing;
}
void MovingCamera::handleMouseMotion(int x, int y, uint8_t mod) {
	Map<Matrix<double,3,1>> dquat { dquat_ };
	if (mouseDown and (lastX !=0 or lastY !=0)) {
		dquat(0) += (x - lastX) * .1f;
		dquat(1) += (y - lastY) * .1f;
	}
	lastX = x;
	lastY = y;
	printf(" - handle mouse motion %d %d\n", x,y);
}
void MovingCamera::handleKey(uint8_t key, uint8_t mod, bool isDown) {
	if (isDown) {
		Map<Matrix<double,3,1>> acc { acc_ };
		Map<const Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );

		Vector3d dacc { Vector3d::Zero() };
		if (isDown and key == VKK_A) dacc(0) += -1;
		if (isDown and key == VKK_D) dacc(0) +=  1;
		if (isDown and key == VKK_W) dacc(2) +=  1;
		if (isDown and key == VKK_S) dacc(2) += -1;

		// Make accel happen local to frame
		acc += viewInv.topLeftCorner<3,3>() * dacc;
	}
}
void MovingCamera::step(double dt) {
	Map<Matrix<double,3,1>> vel { vel_ };
	Map<Matrix<double,3,1>> acc { acc_ };
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };

	std::cout << " [MovingCamera::step()]\n";
	std::cout << "      - vel " << vel.transpose() << "\n";
	std::cout << "      - acc " << acc.transpose() << "\n";
	std::cout << "      - pos " << viewInv.topRightCorner<3,1>().transpose() << "\n";
	std::cout << "      - z+  " << viewInv.block<3,1>(0,2).transpose() << "\n";
	std::cout << "      - q   " << quat.coeffs().transpose() << "\n";

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

MovingCamera::MovingCamera(const CameraSpec& spec) : Camera(spec) {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}
MovingCamera::MovingCamera() {
	Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
	Map<Matrix<double,4,4,RowMajor>> view ( view_ );

	quat_[0] = quat_[1] = quat_[2] = 0;
	quat_[3] = 1;
	viewInv.setIdentity();
	view.setIdentity();
	recompute_view();
}

void RenderState::frameBegin() {
	mstack.reset();
	mstack.push(camera->proj());
	mstack.push(camera->view());
}
