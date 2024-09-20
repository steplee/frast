#pragma once

#include "../gt.h"

#include "frast2/coordinates.h"
#include "dataset.h"


namespace frast {

class AppConfig;

struct __attribute__((packed)) GdalPackedVertex {
	float x,y,z,w;
	float u,v;
};

struct GdalCoordinate;
struct GdalRenderer;
struct GdalTile;
struct GdalDataLoader;
struct GdalObbMap;
struct GdalPushConstants;
struct GdalTypes {
	using Tile = GdalTile;
	using DataLoader = GdalDataLoader;
	using Coord = GdalCoordinate;
	using Renderer = GdalRenderer;
	using ObbMap = GdalObbMap;
	using PushConstants = GdalPushConstants;

	// Defaults
	using BoundingBox = GtOrientedBoundingBox;
	using UpdateContext = GtUpdateContext<GdalTypes>;
	using RenderContext = GtRenderContext<GdalTypes>;

	struct Config {
		static constexpr uint64_t texSize = 256;
		static constexpr uint64_t vertsAlongEdge = 8;
		static constexpr uint64_t vertsPerTile = vertsAlongEdge * vertsAlongEdge;

		bool allowCaster = true;
		bool debugMode = false;
		float sseThresholdClose=.9f, sseThresholdOpen=1.5f;
		std::vector<std::string> obbIndexPaths;
		std::vector<std::string> colorDsetPaths;
		std::string elevDsetPath  = "/data/elevation/srtm/usa.lzw.x1.halfRes.tiff";
	};
	
	struct __attribute__((packed)) GlobalBuffer {
		float mvp[16];
		float positionOffset[4];
		// float modelMats[16*GT_NUM_TILES];
		// float uvScalesAndOffs[4*GT_NUM_TILES];
	};
	static_assert(sizeof(GlobalBuffer) < 65536);

	struct DecodedCpuTileData {
		// alignas(16) double modelMat[16];
		struct MeshData {
			std::vector<GdalPackedVertex> vert_buffer_cpu;
			std::vector<uint16_t> ind_buffer_cpu;
			std::vector<uint8_t> img_buffer_cpu;
			std::vector<uint8_t> tmp_buffer;
			uint32_t texSize[3];
			float uvOffset[2];
			float uvScale[2];
			int layerBounds[10];
		};
		// std::vector<MeshData> meshes;
		// float metersPerTexel;
		MeshData mesh;
	};


	//constexpr static bool unnormalizedTextureCoords = true;
	constexpr static bool unnormalizedTextureCoords = false;
};

struct GdalCoordinate : public BlockCoordinate {
	constexpr static uint32_t SerializedSize = 8;

	using BlockCoordinate::BlockCoordinate;

	// Deserialize
	inline GdalCoordinate(const std::string& a) : BlockCoordinate(*(const uint64_t*)(a.data())) {
	}

	inline GdalCoordinate parent() const {
		return GdalCoordinate {z()-1, y()/2, x()/2};
		// return GdalCoordinate {z()+1, y()/2, x()/2};
	}

	inline std::string toString() const {
		// return std::to_string(c);
		char buf[64];
		sprintf(buf, "(%lu %lu %lu)", z(),y(),x());
		return std::string{buf};
	}

	bool operator==(const GdalCoordinate& b) const {
		return c == b.c;
	}
	inline int level() const {
		return z();
	}

	// Used for fvkGtGenSets, returns all possible children
	std::vector<GdalCoordinate> enumerateChildren() const;

	// Note: This is completely different from RtCoordinate
	inline bool operator<(const GdalCoordinate& o) const {
		return c < o.c;
	}

	// WARNING: Not correct -- must multiply by dataset base size (level is not global anymore.)
	inline float geoError() const {
		// fmt::print("geoError() -- fixme\n");
		return (4.0f / 255.f) / (1 << level());
	}

	struct Hash {
		inline uint64_t operator()(const GdalCoordinate& nc) const {
			return nc.c;
		}
	};
};

struct GdalDataLoader : public GtDataLoader<GdalTypes, GdalDataLoader> {

	// Unfortunately, we need an adaptor to the generic constructor :(
	GdalDataLoader();
	~GdalDataLoader();

	int loadTile(GdalTile* tile, GdalTypes::DecodedCpuTileData& td, bool isOpen);

	void do_init();


	private:
		void loadColor(GdalTile* tile, GdalTypes::DecodedCpuTileData::MeshData& mesh);
		void loadElevAndMetaWithDted(GdalTile* tile, GdalTypes::DecodedCpuTileData::MeshData& mesh, const GdalTypes::Config& cfg);

		cv::Mat colorBuf;
		cv::Mat elevBuf;

		std::vector<std::unique_ptr<GdalDataset>> colorDsets;
		std::unique_ptr<GdalDataset> elevDset;
};

struct GdalTypes;
struct GdalCoordinate;
struct GdalObbMap : public GtObbMap<GdalTypes, GdalObbMap> {

	inline GdalObbMap(const std::vector<std::string>& paths) : GtObbMap<GdalTypes, GdalObbMap>(paths) {}

};

struct GdalTile : public GtTile<GdalTypes, GdalTile> {

	inline GdalTile(const GdalCoordinate& coord_) : GtTile<GdalTypes,GdalTile>(coord_) {
		// constexpr float R1         = (6378137.0f);
		// geoError = (1.f) / static_cast<float>(1 << coord.level());
		geoError = coord_.geoError();
	}

	inline void createChildren(typename GdalTypes::UpdateContext& gtuc) {
		auto &obbMap = gtuc.obbMap;
		for (uint64_t i=0; i<4; i++) {
			uint64_t dx = i % 2;
			uint64_t dy = (i/2) % 2;
			typename GdalTypes::Coord c { coord.z()+1, coord.y()*2+dx, coord.x()*2+dy };
			// typename GdalTypes::Coord c { coord.z()-1, coord.y()*2+dx, coord.x()*2+dy };
			if (coord.z() > 0 and obbMap.tileExists(c)) {
				auto t = new typename GdalTypes::Tile(c);
				t->bb = obbMap.get(c);
				t->parent = this;
				children.push_back(t);
			}
		}
	}

	bool upload(GdalTypes::DecodedCpuTileData& dctd, int idx, GtTileData& td);

	void doRender(GtTileData& td, int meshIdx);
	void doRenderCasted(GtTileData& td, const CasterStuff& casterStuff, int meshIdx);
};


struct GdalPushConstants {
	static constexpr bool Enabled = false;
	inline GdalPushConstants(const GdalTile* tile, uint32_t idx) {};
};

bool maybeCreateObbFile(const GdalTypes::Config& cfg);

struct GdalRenderer : public GtRenderer<GdalTypes, GdalRenderer> {
	// void initPipelinesAndDescriptors(TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);
	void initPipelinesAndDescriptors(const AppConfig& cfg);

	// inline GdalRenderer(const GdalTypes::Config& cfg) : createdObbMapThisRun(maybeCreateObbFile(cfg)), GtRenderer<GdalTypes,GdalRenderer>(cfg) {
	inline GdalRenderer(const GdalTypes::Config& cfg) : GtRenderer<GdalTypes,GdalRenderer>(cfg) {
	}

	virtual ~GdalRenderer();

	int colorChannels = 4;
	bool createdObbMapThisRun = false;
};


}
