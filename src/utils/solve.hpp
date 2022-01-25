#include <Eigen/Core>
#include <Eigen/Cholesky>
#include <Eigen/Geometry>
#include <iostream>

namespace {


	// Two common ways of solving:
	//		1) DLT: form 9x9 matrix, get solution as min-norm vector with SVD
	//		2) Inhomogeneous 8x8 solve, which works well except when near singularity
	//
	// Go with (2) here.
	// See multiple view geo book, section 4.4.4.
	//
	void solveHomography(float H[9], const float A_[8], const float B_[8]) {
		Eigen::Matrix<float, 8,8> M;
		Eigen::Matrix<float, 8,1> b;
		Eigen::Map<const Eigen::Matrix<float, 4,2, Eigen::RowMajor>> A { A_ };
		Eigen::Map<const Eigen::Matrix<float, 4,2, Eigen::RowMajor>> B { B_ };

		Eigen::Vector2f a_mu = A.colwise().mean();
		Eigen::Vector2f b_mu = B.colwise().mean();
		//float a_scale = (A.rowwise() - a_mu.transpose()).rowwise().norm().mean();
		//float b_scale = (B.rowwise() - b_mu.transpose()).rowwise().norm().mean();
		float a_scale = 0, b_scale = 0;
		for (int i=0; i<4; i++) {
			a_scale += .25f * (A.row(i) - a_mu.transpose()).norm();
			b_scale += .25f * (B.row(i) - b_mu.transpose()).norm();
		}

		Eigen::Matrix<float, 3,3> Ta; Ta <<
			1.f/a_scale, 0, -a_mu(0)/a_scale,
			0, 1.f/a_scale, -a_mu(1)/a_scale,
			0, 0, 1.f;
		Eigen::Matrix<float, 3,3> Tb; Tb <<
			1.f/b_scale, 0, -b_mu(0)/b_scale,
			0, 1.f/b_scale, -b_mu(1)/b_scale,
			0, 0, 1.f;

		/*
		Eigen::Matrix<float, 3,3> Ta; Ta <<
			a_scale, 0, a_mu(0),
			0, a_scale, a_mu(1),
			0, 0, 1.f;
		Eigen::Matrix<float, 3,3> Tb; Tb <<
			b_scale, 0, b_mu(0),
			0, b_scale, b_mu(1),
			0, 0, 1.f;
		Ta = Ta.inverse().eval();
		Tb = Tb.inverse().eval();
		*/


		Eigen::Matrix<float,4,2> A1 = (A.rowwise().homogeneous() * Ta.transpose()).leftCols(2);
		Eigen::Matrix<float,4,2> B1 = (B.rowwise().homogeneous() * Tb.transpose()).leftCols(2);

		for (int i=0; i<4; i++) {
			float ax = A1(i,0), ay = A1(i,1);
			float bx = B1(i,0), by = B1(i,1);
			const float w = 1.0f;

			M.block<2,8>(i*2, 0) <<
				0,0,0, -w*ax, -w*ay, -w*w, by*ax, by*ay,
				w*ax, w*ay, w*w, 0, 0, 0, -bx*ax, -bx*ay;

			b.segment<2>(i*2) << -w*by, w*bx;
		}

		// Cannot use: matrix probably not posdef
		//Eigen::Matrix<float,8,1> x = M.ldlt().solve(b);
		//Eigen::Matrix<float,8,1> x = M.householderQr().solve(b);
		Eigen::Matrix<float,8,1> x = M.fullPivLu().solve(b);

		Eigen::Matrix<float, 3,3> H1;
		for (int i=0; i<8; i++) H1(i/3,i%3) = x(i);
		H1(2,2) = 1.0f;

		Eigen::Matrix<float, 3,3> H2 = Tb.inverse() * H1 * Ta;
		H2 /= H2(2,2);

		for (int i=0; i<8; i++) H[i] = H2(i/3,i%3);
		H[8] = 1.f;

		/*
		std::cout << " - A:\n" << A << "\n";
		std::cout << " - B:\n" << B << "\n";
		std::cout << " - A1:\n" << A1 << "\n";
		std::cout << " - B1:\n" << B1 << "\n";
		std::cout << " - Ta:\n" << Ta << "\n";
		std::cout << " - Tb:\n" << Tb << "\n";
		std::cout << " - M:\n" << M << "\n";
		std::cout << " - |M|:\n" << M.determinant() << "\n";
		std::cout << " - b:" << b.transpose() << "\n";
		std::cout << " - x:" << x.transpose() << "\n";
		std::cout << " - H2:\n" << H2 << "\n";
		*/

	}

};
