#pragma once

#include <Eigen/StdVector>
#include "frastVk/gt/gt.h"
#include "frastVk/gt/rt/rt.h"
#include "frastVk/extra/headlessCopyHelper.hpp"
#include "thirdparty/nlohmann/json.hpp"

#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <random>

// frast/image.h defines:
//    using EncodedImage = std::vector<uint8_t>;
//    struct EncodedImageRef { size_t len; uint8_t* data; };
//    bool encode_jpeg(EncodedImage& eimg, const Image& img);
//    bool decode_jpeg(Image& out, const EncodedImageRef& eimg);
#include "frast/image.h"

using namespace Eigen;

namespace  {


// false on success (EEXIST counts as success)
bool mkdirs(const std::string& d) {
	int stat = mkdir(d.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IXOTH);
	if (stat and errno == EEXIST) {
		fmt::print(fmt::fg(fmt::color::steel_blue), " - Dir '{}' already exists.\n", d);
		return false;
	}
	else if (stat and errno == ENOENT) {
		fmt::print(fmt::fg(fmt::color::steel_blue), " - No path to '{}', creating subdirs.\n", d);
		int e = d.rfind("/");
		if (e == std::string::npos) {
			fmt::print(" - Failed to make dir... no more substrings. Err {}\n", d, strerror(errno));
		}
		while (e>1 and d[e-1] == '/') e--;
		std::string dd = d.substr(0,e);
		if (mkdirs(dd)) {
			fmt::print(" - Failed to make dir '{}' (subcall for '{}' failed)\n", d, dd);
		} else {
			stat = mkdir(d.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IXOTH);
			if (stat) {
				fmt::print(" - Failed to make dir '{}' (subcall for '{}' succeeded, then failed with {})\n", d, dd, strerror(errno));
				return true;
			}
		}
	} else if (stat) {
		fmt::print(fmt::fg(fmt::color::steel_blue), " - Failed mkdirs '{}':: {}\n", d, strerror(errno));
		return true;
	}
	return false;
}

RowMatrix3d getLtp(const Vector3d& eye) {
	RowMatrix3d R;
	R.col(2) = (eye).normalized();
	R.col(0) = Vector3d::UnitZ().cross(R.col(2)).normalized();
	R.col(1) = -R.col(0).cross(R.col(2)).normalized();
	// fmt::print(" - LTP det {}\n", R.determinant());
	return R;
}




using Pose = RowMatrix4d;
struct BasePose {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	Pose pose;
	Vector3d eye, tgt, up;

	inline operator Pose&() { return pose; }
	inline operator const Pose&() const { return pose; }
};

struct Set {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	std::vector<Pose> poses;
	std::vector<CameraSpec> camInfos;
};

struct SetAppCamera : public Camera {
	public:
		inline SetAppCamera() : Camera() {}
		inline SetAppCamera(const CameraSpec& spec) : Camera(spec) {}
		inline virtual ~SetAppCamera() {}

		inline virtual void setPosition(double* t) override { assert(false); }
		inline virtual void setRotMatrix(double* R) override { assert(false); }
		inline virtual void step(double dt) override { }

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override { return false; }
		inline virtual bool handleMousePress(int button, int action, int mods) override { return false; }
		inline virtual bool handleMouseMotion(double x, double y) override { return false; }

		inline void set(const Pose& pose, const CameraSpec& camInfo) {
			Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
			viewInv = pose;
			recompute_view();
			spec_ = camInfo;
			compute_projection();
		}
		inline void recompute_view() {
			Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
			Map<Matrix<double,4,4,RowMajor>> view ( view_ );
			view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
			view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
			view.row(3) << 0,0,0,1.;
		}

	protected:
};

}
