#pragma once

#include "frast/image.h"
#include "buffer.hpp"
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
		RowMatrix4f V;
};

struct RaytracerConfig {
	int samplesPerPixel = 4;
	int depth = 4;
};

struct __attribute__((packed)) Vertex {
	float x,y,z;
	float nx,ny,nz;
	float u,v;
	uint16_t objId;
};
struct __attribute__((packed)) Pixel {
	uint8_t r,g,b;
	uint8_t rough1;
};

struct Tile {
	/*
	int numVerts=0;
	int texWidth=0, texHeight=0;
	Vertex* verts=0;
	Pixel* pixels=0;
	bool isCuda = false;

	void allocateCpu();
	void allocateGpu();
	~Tile();
	*/

	Buffer<Vertex> verts;
	Buffer<Pixel> pixels;
	Buffer<uint16_t> indices;

};

using Geometry = std::vector<Tile>;

/*
struct Geometry {
	bool isCuda = false;

	// Number of vertices, cells (triangles), and objects
	int nv=0, nc=0, no=0;

	// 1 per vertex
	float* positions=0; // 3
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
*/

struct RenderResult {
	Image rgb;
	Image depth;
};

struct __attribute__((packed)) Ray {
	float p[3];
	float d[3];
};

class Raytracer {

	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

		Raytracer();

		RenderResult render();

		void setGeometry(Geometry&& geom);


	private:
		void make_samples();
		void trace();
		RenderResult finalizeResult(); // Convert float to uint8_t, render depth map


	private:
		int w=512, h=512;

		Buffer<Ray> rays;

		Geometry geom;

		// The place where trace() accumulates too
		Buffer<Vector3f> acc;

		Camera cam;
		RaytracerConfig cfg;
};
