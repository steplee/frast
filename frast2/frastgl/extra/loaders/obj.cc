#include "obj.h"

#include <fstream>
#include <cstring>

#include <vector>
#include <unordered_map>
#include <string>

#include <fmt/core.h>


namespace frast {

// This was adapted from my SYCL raytracer, and the implementation is not ideal...
//
// If using blender, be sure to triangulate mesh before exporting.
// The face parsing will silently fail.

struct float2 { float x,y; };
struct float3 { float x,y,z; };
struct int3 { int x,y,z; };

ObjLoader::ObjLoader(const std::string& path) {
	std::ifstream ifs (path);
	if (not ifs.good()) {
		fmt::print(" - failed to open '{}'\n",path);
		assert(false);
	}
	std::string line;

	int objectCnt = 0;

	std::string name = "obj_" + std::to_string(objectCnt);

	std::vector<float3> objPositions;
	std::vector<float3> objNormals;
	std::vector<float2> objUvs;
	std::vector<int3> objInds;

	std::vector<float3> finalPositions;
	std::vector<float3> finalNormals;
	std::vector<float2> finalUvs;
	std::vector<int3> finalInds;

	std::unordered_map<std::string,int> vrtx2indx;

	auto push_object = [&]() {
		if (finalPositions.size()) {

			fmt::print(" - Pushing obj mesh '{}', {} verts [{} {}], {} tris\n", name,
					finalPositions.size(),
					finalUvs.size(), finalNormals.size(),
					finalInds.size());
			Object obj;
			obj.name = name;

			VertexLayout vl;
			vl.dimsPos = 3;
			vl.dimsUv = finalUvs.size() ? 2 : 0;
			vl.dimsNormal = finalNormals.size() ? 3 : 0;
			std::vector<uint8_t> vert_bytes;
			int nv = finalPositions.size();
			vert_bytes.resize(nv * vl.byteSize());
			int offset = 0;
			for (int i=0; i<nv; i++) {
				memcpy(vert_bytes.data() + offset, &finalPositions[i].x, 4); offset += 4;
				memcpy(vert_bytes.data() + offset, &finalPositions[i].y, 4); offset += 4;
				memcpy(vert_bytes.data() + offset, &finalPositions[i].z, 4); offset += 4;
				if (vl.dimsUv) memcpy(vert_bytes.data() + offset, &finalUvs[i].x, 4); offset += 4;
				if (vl.dimsUv) memcpy(vert_bytes.data() + offset, &finalUvs[i].y, 4); offset += 4;
				if (vl.dimsNormal) memcpy(vert_bytes.data() + offset, &finalNormals[i].x, 4); offset += 4;
				if (vl.dimsNormal) memcpy(vert_bytes.data() + offset, &finalNormals[i].y, 4); offset += 4;
				if (vl.dimsNormal) memcpy(vert_bytes.data() + offset, &finalNormals[i].z, 4); offset += 4;
			}
			float *vertData = (float*)vert_bytes.data();
			/*for (int i=0; i<10; i++) {
				fmt::print(" vert[{}] ",i);
				for (int j=0; j<vl.byteSize()/4; j++) {
					fmt::print("{} ", vertData[i*vl.byteSize()/4+j]);
				}
				fmt::print("\n");
			}*/
			obj.mesh.uploadVerts(std::move(vert_bytes), vl);

			int nc = finalInds.size();
			std::vector<uint8_t> ind_bytes;
			ind_bytes.resize(nc * 4 * 3);
			offset = 0;
			for (int i=0; i<nc; i++) {
				memcpy(ind_bytes.data() + offset, &finalInds[i].x, 4); offset += 4;
				memcpy(ind_bytes.data() + offset, &finalInds[i].y, 4); offset += 4;
				memcpy(ind_bytes.data() + offset, &finalInds[i].z, 4); offset += 4;
			}
			obj.mesh.uploadInds(std::move(ind_bytes), GL_UNSIGNED_INT);

			root.children.push_back(std::move(obj));
		} else {
			fmt::print(" - called push_object, but had no vertices!\n");
		}


		objPositions.clear();
		objNormals.clear();
		objUvs.clear();
		objInds.clear();
		finalPositions.clear();
		finalNormals.clear();
		finalUvs.clear();
		finalInds.clear();
		vrtx2indx.clear();
	};

	while (ifs.good()) {
		std::getline(ifs, line);
		if (line.length() <= 2) continue;

		float fx,fy,fz;

		if (line[0] == 'o') {
			push_object();
			name = line.substr(2);
		}

		if (line[0] == 'v' and line[1] == ' ') {
			int nr = sscanf(line.c_str(), "v %f %f %f", &fx, &fy, &fz);
			assert(nr == 3);
			objPositions.push_back(float3{fx,fy,fz});
		}

		else if (line[0] == 'v' and line[1] == 't') {
			int nr = sscanf(line.c_str(), "vt %f %f", &fx, &fy);
			assert(nr == 2);
			objUvs.push_back(float2{fx,fy});
		}

		else if (line[0] == 'v' and line[1] == 'n') {
			int nr = sscanf(line.c_str(), "vn %f %f %f", &fx, &fy, &fz);
			assert(nr == 3);
			objNormals.push_back(float3{fx,fy,fz});
		}

		else if (line[0] == 'f' and line[1] == ' ') {
			int i,j,k;
			int ti=-1,tj,tk;
			int ni=-1,nj,nk;
			if (3 == sscanf(line.c_str(), "f %d %d %d", &i, &j, &k)) {
			} else if (6 == sscanf(line.c_str(), "f %d/%d %d/%d %d/%d", &i, &ti, &j, &tj, &k, &tk)) {
			} else if (9 == sscanf(line.c_str(), "f %d/%d/%d %d/%d/%d %d/%d/%d", &i,&ti,&ni, &j,&tj,&nj, &k,&tk,&nk)) {
			}

			i--; j--; k--;
			ti--; tj--; tk--;
			ni--; nj--; nk--;
			assert(i >= 0 and i < objPositions.size());
			assert(j >= 0 and j < objPositions.size());
			assert(k >= 0 and k < objPositions.size());

			char aa[20], bb[20], cc[20];
			sscanf(line.c_str(), "f %s %s %s", &aa, &bb, &cc);
			std::string a(aa), b(bb), c(cc);

			if (vrtx2indx.find(a) == vrtx2indx.end()) {
				vrtx2indx[a] = finalPositions.size();
				finalPositions.push_back(objPositions[i]);
				if (ni >= 0) finalNormals.push_back(objNormals[ni]);
				if (ti >= 0) finalUvs.push_back(objUvs[ni]);
			}
			if (vrtx2indx.find(b) == vrtx2indx.end()) {
				vrtx2indx[b] = finalPositions.size();
				finalPositions.push_back(objPositions[j]);
				if (ni >= 0) finalNormals.push_back(objNormals[ni]);
				if (ti >= 0) finalUvs.push_back(objUvs[ni]);
			}
			if (vrtx2indx.find(c) == vrtx2indx.end()) {
				vrtx2indx[c] = finalPositions.size();
				finalPositions.push_back(objPositions[k]);
				if (ni >= 0) finalNormals.push_back(objNormals[ni]);
				if (ti >= 0) finalUvs.push_back(objUvs[ni]);
			}
			finalInds.push_back(int3{vrtx2indx[a],vrtx2indx[b],vrtx2indx[c]});
		} else {
			fmt::print(" - skipping line '{}'\n", line);
		}
	}
	push_object();

	fmt::print(" - Done loading obj file '{}'. Root has {} children\n", path, root.children.size());

}

}
