
#include <cassert>
#include <cmath>
#include <fmt/core.h>
#include "ellipsoid.h"

#include "frast2/frastgl/utils/eigen.h"
#include "frast2/frastgl/utils/guards.hpp"
#include <Eigen/SVD>


namespace {
	// Actually I just shade with the (normalized) Z coordinate in camera space.
	// This has the intended effect of making the sphere be silhoutted (only edges visible),
	// and I can avoid the complicated normal transformations.
std::string ellps_vsrc = R"(#version 440
	in layout(location=0) vec3 a_pos;
	in layout(location=1) vec3 a_nrl;

	uniform layout(location=0) mat4 u_mvp;
	// uniform layout(location=1) mat4 u_model;
	// uniform layout(location=2) mat4 u_viewInv;

	out vec3 v_nrl;

	void main() {
		gl_Position = u_mvp * vec4(a_pos, 1.);
		// v_nrl = transpose(mat3(u_mvp)) * a_nrl;
		v_nrl = (mat3(u_mvp)) * a_nrl;
	})";

std::string ellps_fsrc = R"(#version 440
	in vec3 v_nrl;

	uniform layout(location=0) mat4 u_mvp;
	// uniform layout(location=1) mat4 u_model;
	// uniform layout(location=2) mat4 u_viewInv;
	uniform layout(location=3) vec4 u_color;

	out vec4 o_color;

	void main() {
		vec4 col = u_color;

		// vec3 zplus = vec3( u_viewInv[0][2], u_viewInv[1][2], u_viewInv[2][2]);
		// vec3 zplus = vec3( u_viewInv[2][0], u_viewInv[2][1], u_viewInv[2][2]);

		// col.a *= dot(v_nrl, zplus);
		// col.a *= 1. - abs(dot(normalize(v_nrl), zplus));
		col.a *= pow(1. - abs(normalize(v_nrl).z),2.);

		o_color = col;
		// o_color = vec4(1.);
	})";
}

namespace frast {



		Ellipsoid::Ellipsoid()
			: ellpsShader(ellps_vsrc, ellps_fsrc)
		{
			int H = 12;
			int W = 20;
			constexpr float pi = static_cast<float>(M_PI);
			for (int y=0; y<H; y++) {
				for (int x=0; x<W; x++) {
					float v = ((float)y) / (H-1);
					float u = ((float)x) / (W-1);

					float Z = -cos(v * pi);
					float Y = cos(u*pi*2.f)*-sin(v*pi);
					float X = sin(u*pi*2.f)*-sin(v*pi);

					verts.push_back(X);
					verts.push_back(Y);
					verts.push_back(Z);

					uint16_t I = y*W+x;
					uint16_t J = y*W + (x+1)%W;
					uint16_t K = ((y+1)%H)*W + (x+1)%W;
					uint16_t L = ((y+1)%H)*W + (x )%W;
					inds.push_back(J);
					inds.push_back(I);
					inds.push_back(K);

					inds.push_back(L);
					inds.push_back(K);
					inds.push_back(I);
				}
			}

			for (int i=0; i<16; i++) M[i] = i % 5 == 0;
		}

		void Ellipsoid::render(const RenderState& rs0) {
			// GLboolean enable_cull;
			// glGetBooleanv(GL_CULL_FACE, &enable_cull);
			// glEnable(GL_CULL_FACE);

			BlendGuard bf(true, GL_ADD, GL_ADD);

			glUseProgram(ellpsShader.prog);

			RenderState rs{rs0};
			rs.pushModel(M);

			float mvp[16];
			// float viewInv[16]; // My shader actually doesn't need this
			// for (int i=0; i<16; i++) viewInv[i] = rs.viewInv()[i];
			rs.computeMvpf(mvp);
			glUniformMatrix4fv(0, 1, true, mvp);
			// glUniformMatrix4fv(1, 1, true, M);
			// glUniformMatrix4fv(2, 1, true, viewInv);
			glUniform4fv(3, 1, color);

			// FIXME: Use vbo.
			//
			// Setup pos/nrl (normals just alias position)
			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*4, (void*)verts.data());
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*4, (void*)verts.data());

			glDrawElements(GL_TRIANGLES, inds.size(), GL_UNSIGNED_SHORT, inds.data());

			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
			glUseProgram(0);
			// if (!enable_cull) glDisable(GL_CULL_FACE);
		}

		void Ellipsoid::setColor(const float c_[4]) {
			memcpy(color,c_,4*4);
		}
		void Ellipsoid::setModel(const float M_[16]) {
			memcpy(M,M_,4*16);
		}
		void Ellipsoid::setModelFromInvViewAndCov(const float VI_[16], const float C_[9]) {
			using namespace Eigen;
			Map<RowMatrix4f> O(M);
			Map<const RowMatrix4f> VI(VI_);
			Map<const RowMatrix3f> C(C_);

			// The matrix C must be positive definite and symmetric.
			// Therefore the SVD (USVt) is the same as the eigendecomp (QLQ^-1)
			// The singular values are the same as the eigenvalues,
			// and the standard deviations ("sigmas") are the square roots of these.
			// The size of the ellipsoid should be the same of each sigma, rotated properly.
			Eigen::JacobiSVD<RowMatrix3f> svd(C, ComputeFullU|ComputeFullV);
			RowMatrix3f U     = svd.matrixU();
			RowMatrix3f sqrtS = (svd.singularValues().array().sqrt()).matrix().asDiagonal();
			RowMatrix3f V     = svd.matrixV();

			RowMatrix3f scale = U * sqrtS * V.transpose();
			O.noalias() = VI;
			O.topLeftCorner<3,3>() *= scale;
		}

}
