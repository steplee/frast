#pragma once

#include "trace.h"

struct __attribute__((packed)) PackedVertex {
	uint8_t x,y,z,w;
	uint16_t u,v;
	uint8_t nx,ny,nz;
	uint8_t extra;
};
static_assert(sizeof(PackedVertex) == 12);

struct DecodedCpuTileData {
	alignas(16) double modelMat[16];
	struct MeshData {
		std::vector<PackedVertex> vert_buffer_cpu;
		std::vector<uint16_t> ind_buffer_cpu;
		std::vector<uint8_t> img_buffer_cpu;
		std::vector<uint8_t> tmp_buffer;
		uint32_t texSize[3];
		float uvOffset[2];
		float uvScale[2];
		int layerBounds[10];
	};
	std::vector<MeshData> meshes;
	float metersPerTexel;
};

struct LoadedInformation {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW;
	RowMatrix4d model;
	RowMatrix3d ltp;
	RowMatrix4d recommendView;
	Vector3d ctr0, ctr1;
};

LoadedInformation load_to(std::vector<Tile>& outs, const std::string& filename);
