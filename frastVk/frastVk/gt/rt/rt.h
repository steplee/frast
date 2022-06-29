#pragma once

#include "../gt.h"

class AppConfig;
class Device;
class SimpleRenderPass;

/*
 *
 * Rt test
 *
 */

struct __attribute__((packed)) PackedVertex {
	uint8_t x,y,z,w;
	uint16_t u,v;
	uint8_t nx,ny,nz;
	uint8_t extra;
};
static_assert(sizeof(PackedVertex) == 12);


/*
struct RtBoundingBox : public GtBoundingBox<RtTypes, RtBoundingBox> {
	inline float computeSse(const GtUpdateContext<RtTypes>& gtuc) const {
		return 0.f;
	}
};
*/


struct RtCoordinate;
struct RtRenderer;
struct RtTile;
struct RtDataLoader;
struct RtObbMap;
struct RtPushConstants;
struct RtTypes {
	using Tile = RtTile;
	using DataLoader = RtDataLoader;
	using Coord = RtCoordinate;
	using Renderer = RtRenderer;
	using ObbMap = RtObbMap;
	using PushConstants = RtPushConstants;
	//using BoundingBox = RtBoundingBox;

	// Defaults
	using BoundingBox = GtBoundingBox<RtTypes>;
	using UpdateContext = GtUpdateContext<RtTypes>;
	using RenderContext = GtRenderContext<RtTypes>;


	struct GlobalBuffer {
		float mvp[16];
		float positionOffset[4];
		float modelMats[16*GT_NUM_TILES];
		float uvScalesAndOffs[4*GT_NUM_TILES];
	};

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


	constexpr static bool unnormalizedTextureCoords = true;

};

struct RtCoordinate {
	constexpr static uint32_t MAX_LEN = 26;
	constexpr static uint32_t Size = 26;
	char key[MAX_LEN];
	uint8_t len = 0;

	inline RtCoordinate() {
		memset(key, 0, MAX_LEN);
		len = 0;
	}
	inline RtCoordinate(const std::string& s) {
		int i =0;
		for (i=0; i<MAX_LEN; i++) {
			if ((uint8_t)s[i] == 255 or s[i] == 0 or i >= s.length()) break;
			key[i] = s[i];
		}
		len = i;
	}
	inline RtCoordinate(const RtCoordinate& other, char next) {
		memcpy(key, other.key, MAX_LEN);
		key[other.len] = next;
		len = other.len + 1;
	}

	inline RtCoordinate parent() const {
		RtCoordinate out;
		for (int i=0; i<len-1; i++) out.key[i] = key[i];
		out.len = len-1;
		return out;
	}

	inline std::string toString() const {
		return std::string{key, len};
	}

	bool operator==(const RtCoordinate& b) const {
		if (b.len != len) return false;
		for (int i=0; i<len; i++) if (key[i] != b.key[i]) return false;
		return true;
	}
	inline int level() const {
		return len;
	}
	struct Hash {
		inline uint64_t operator()(const RtCoordinate& nc) const {
			uint64_t acc = 0;
			for (int i=0; i<nc.len and nc.key[i] != 0; i++) acc = acc ^ (nc.key[i] * 777lu) + (acc << 3);
			return acc;
		}
	};
};

struct RtDataLoader : public GtDataLoader<RtTypes, RtDataLoader> {

	std::string rootDir = "/data/gearth/tpAois_wgs/";

	// Unfortunately, we need an adaptor to the generic constructor :(
	inline RtDataLoader(typename RtTypes::Renderer& renderer_) : GtDataLoader<RtTypes, RtDataLoader>(renderer_) {
	}

	int loadTile(RtTile* tile, RtTypes::DecodedCpuTileData& td);

	/*
	inline bool uploadTile(RtTile* tile, RtTypes::DecodedCpuTileData::MeshData& dctd, GtTileData& td) {
		tile->loaded = true;
		return false;
	}
	*/

};

struct RtTypes;
struct RtCoordinate;
struct RtObbMap : public GtObbMap<RtTypes, RtObbMap> {

	inline RtObbMap(const std::string& path) : GtObbMap<RtTypes, RtObbMap>(path) {}

	inline bool tileIsTerminal(const RtCoordinate& coord) {
		for (int i=0; i<8; i++) {
			RtCoordinate c{coord,(char)(i+'0')};
			if (tileExists(c)) return false;
		}
		return true;
	}
};

struct RtTile : public GtTile<RtTypes, RtTile> {

	inline RtTile(const RtCoordinate& coord_) : GtTile<RtTypes,RtTile>(coord_) {
		constexpr float R1         = (6378137.0f);
		// geoError = (1.f / R1) / static_cast<float>(1 << coord.level());
		geoError = (1.f) / static_cast<float>(1 << coord.level());
	}

	inline void createChildren(typename RtTypes::UpdateContext& gtuc) {
		auto &obbMap = gtuc.obbMap;
		for (char next='0'; next<'8'; next++) {
			typename RtTypes::Coord c { coord, next };
			if (obbMap.tileExists(c)) {
				auto t = new typename RtTypes::Tile(c);
				t->bb = obbMap.get(c);
				t->parent = this;
				children.push_back(t);
			}
		}
	}

	bool upload(RtTypes::DecodedCpuTileData::MeshData& dctd, GtTileData& td);

	std::vector<float> uvInfos;
	void updateGlobalBuffer(RtTypes::GlobalBuffer* gpuBuffer);
};

struct RtPushConstants {
	uint32_t index;
	uint32_t octantMask;
	uint32_t level;

	inline RtPushConstants(const RtTile* tile, uint32_t idx) {
		index = idx;
		octantMask = tile->mask;
		level = tile->coord.level();
	}
};

struct RtRenderer : public GtRenderer<RtTypes, RtRenderer> {
	void initPipelinesAndDescriptors(TheDescriptorPool& dpool, SimpleRenderPass& pass, const AppConfig& cfg);

	std::string rootDir = "/data/gearth/tpAois_wgs/";

	virtual ~RtRenderer();
};
