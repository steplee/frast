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

void MatrixStack::push(const double* t, bool increment) {
	if (d == 0) {
		memcpy(m, t, sizeof(double)*16);
		Map<      Matrix<double,4,4,RowMajor>> C ( m+(d)*16 );
		//printf(" - push matrix to depth %d .. done\n", d); fflush(stdout);
		//std::cout << C << std::endl;
	} else {
		assert(d < (MAX_DEPTH-1)); // too deep
		// Note: requires alignment of input!
		Map<const Matrix<double,4,4,RowMajor>> A ( m+(d-1)*16 );
		Map<const Matrix<double,4,4,RowMajor>> B ( t );
		Map<      Matrix<double,4,4,RowMajor>> C ( m+(d)*16 );
		C = A * B;
		//printf(" - push matrix to depth %d .. done\n", d); fflush(stdout);
		//std::cout << B << std::endl;
		//std::cout << C << std::endl;
	}
	if (increment) d++;
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
	// mstack.reset();
	// mstack.push(camera->proj());
	// mstack.push(camera->view());

	Map<Matrix<double,4,4,RowMajor>> P ( proj_ );
	P = Map<const Matrix<double,4,4,RowMajor>> ( camera->proj() );

	Map<Matrix<double,4,4,RowMajor>> MV ( mv_ );
	MV = Map<const Matrix<double,4,4,RowMajor>> ( camera->view() );
}

void RenderState::computeMvp(double* out_) const {
	Map<Matrix<double,4,4,RowMajor>> out ( out_ );
	Map<const Matrix<double,4,4,RowMajor>> P  ( proj_ );
	Map<const Matrix<double,4,4,RowMajor>> MV ( mv_ );
	out = P * MV;
}
void RenderState::computeMvpf(float* out_) const {
	Map<Matrix<float,4,4,RowMajor>> out ( out_ );
	Map<const Matrix<double,4,4,RowMajor>> P  ( proj_ );
	Map<const Matrix<double,4,4,RowMajor>> MV ( mv_ );
	out = (P * MV).cast<float>();
}

void RenderState::pushModel(const double* m) {
	Map<Matrix<double,4,4,RowMajor>> mv ( mv_ );
	Map<const Matrix<double,4,4,RowMajor>> m1 ( m );
	mv = mv * m1;
}
void RenderState::pushModel(const float* m) {
	Map<Matrix<double,4,4,RowMajor>> mv ( mv_ );
	Map<const Matrix<float,4,4,RowMajor>> m1 ( m );
	mv = mv * m1.cast<double>();
}



}
