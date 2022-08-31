#include "frast/image.h"
#include "gpu_buffer.hpp"
#include <Eigen/Core>

// Herein is a very simple raytracer that has just one material (textured, with normals, and optionally roughness textures as well).
// Each ray does a brute-force intersection with the entire scene. No BVHs or anything like that.
// So inputs (\Geometry) should be small.

using RowMatrix3f = Eigen::Matrix<float,3,3,Eigen::RowMajor>;
using RowMatrix4f = Eigen::Matrix<float,4,4,Eigen::RowMajor>;
using namespace Eigen;

struct Camera {
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

		inline int w() const { return wh(0); }
		inline int h() const { return wh(1); }

		Array2i wh;
		Array2f fxy;
		Array2f cxy;
		RowMatrix4f P;
};

struct RaytracerConfig {
	int samplesPerPixel = 4;
	int depth = 4;
};

struct Geometry {
	bool isCuda = false;

	// Number of vertices, cells (triangles), and objects
	int nv, nc, no;

	// 1 per vertex
	float* positions; // 3
	float* normals; // 3
	float* uvs; // 2
	uint16_t* objIds; // 1

	// 1 per index
	uint32_t* indices;

	// 1 per texture
	uint8_t** textures;
	uint8_t** roughness; // 1 (or 2?)
	uint16_t* textureSizes;
};

struct ProcessedGeometry {
};

struct RenderResult {
	Image rgb;
	Image depth;
};

class Raytracer {

	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

		RenderResult render();

		void setGeometry(Geometry&& geom);


	private:
		void make_samples();
		void trace();
		RenderResult finalizeResult(); // Convert float to uint8_t, render depth map


	private:

		Geometry geom;

		// The place where trace() accumulates too
		GpuBuffer<float> acc;

		Camera cam;
		RaytracerConfig cfg;
};


RenderResult Raytracer::render() {
	for (int i = 0; i < cfg.samplesPerPixel; i++) {
		make_samples();
		trace();
	}
	return finalizeResult();
}

void Raytracer::setGeometry(Geometry&& geom_) {
	this->geom = std::move(geom_);
}

void Raytracer::make_samples() {
}

void Raytracer::trace() {
}

RenderResult Raytracer::finalizeResult() {
	RenderResult res;
	return res;
}







int main() {
	return 0;
}
