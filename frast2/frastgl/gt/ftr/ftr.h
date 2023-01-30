#pragma once

#include "../gt.h"
#include "frast2/flat/reader.h"

namespace frast {

class AppConfig;

struct __attribute__((packed)) FtPackedVertex {
	float x,y,z,w;
	float u,v;
};
// static_assert(sizeof(FtPackedVertex) == 12);


struct FtCoordinate;
struct FtRenderer;
struct FtTile;
struct FtDataLoader;
struct FtObbMap;
struct FtPushConstants;
struct FtTypes {
	using Tile = FtTile;
	using DataLoader = FtDataLoader;
	using Coord = FtCoordinate;
	using Renderer = FtRenderer;
	using ObbMap = FtObbMap;
	using PushConstants = FtPushConstants;

	// Defaults
	using BoundingBox = GtOrientedBoundingBox;
	using UpdateContext = GtUpdateContext<FtTypes>;
	using RenderContext = GtRenderContext<FtTypes>;

	struct Config {
		static constexpr uint64_t texSize = 256;
		static constexpr uint64_t vertsAlongEdge = 8;
		static constexpr uint64_t vertsPerTile = vertsAlongEdge * vertsAlongEdge;


		bool allowCaster = true;
		bool debugMode = false;
		float sseThresholdClose=.9f, sseThresholdOpen=1.5f;
		// std::string obbIndexPath;
		// std::string colorDsetPath = "/data/naip/mocoNaip/out.ft";
		std::vector<std::string> obbIndexPaths;
		std::vector<std::string> colorDsetPaths;
		std::string elevDsetPath  = "/data/elevation/gmted/gmted.fft";

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
			std::vector<FtPackedVertex> vert_buffer_cpu;
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

struct FtCoordinate : public BlockCoordinate {
	constexpr static uint32_t SerializedSize = 8;

	using BlockCoordinate::BlockCoordinate;

	// Deserialize
	inline FtCoordinate(const std::string& a) : BlockCoordinate(*(const uint64_t*)(a.data())) {
	}

	inline FtCoordinate parent() const {
		return FtCoordinate {z()-1, y()/2, x()/2};
	}

	inline std::string toString() const {
		// return std::to_string(c);
		char buf[64];
		sprintf(buf, "(%lu %lu %lu)", z(),y(),x());
		return std::string{buf};
	}

	bool operator==(const FtCoordinate& b) const {
		return c == b.c;
	}
	inline int level() const {
		return z();
	}

	// Used for fvkGtGenSets, returns all possible children
	std::vector<FtCoordinate> enumerateChildren() const;

	// Note: This is completely different from RtCoordinate
	inline bool operator<(const FtCoordinate& o) const {
		return c < o.c;
	}

	inline float geoError() const {
		return (4.0f / 255.f) / (1 << level());
	}

	struct Hash {
		inline uint64_t operator()(const FtCoordinate& nc) const {
			return nc.c;
		}
	};
};

struct FtDataLoader : public GtDataLoader<FtTypes, FtDataLoader> {

	// Unfortunately, we need an adaptor to the generic constructor :(
	FtDataLoader(typename FtTypes::Renderer& renderer_);
	~FtDataLoader();

	int loadTile(FtTile* tile, FtTypes::DecodedCpuTileData& td, bool isOpen);


	private:
		void loadColor(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh);
		void loadElevAndMetaWithDted(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh, const FtTypes::Config& cfg);

		/*
		std::vector<MDB_txn*> color_txns;
		MDB_txn* elev_txn = nullptr;
		bool loadTile(FtTile* tile);

		// DatasetReader* colorDset = nullptr;
		std::vector<DatasetReader*> colorDsets;
		DatasetReader* elevDset  = nullptr;

		Image colorBuf;
		Image elevBuf;
		*/
		cv::Mat colorBuf;
		cv::Mat elevBuf;

		std::vector<FlatReaderCached*> colorDsets;
		FlatReaderCached* elevDset  = nullptr;

};

struct FtTypes;
struct FtCoordinate;
struct FtObbMap : public GtObbMap<FtTypes, FtObbMap> {

	inline FtObbMap(const std::vector<std::string>& paths) : GtObbMap<FtTypes, FtObbMap>(paths) {}

	/*inline bool tileIsTerminal(const FtCoordinate& coord) {
		for (int i=0; i<8; i++) {
			FtCoordinate c{coord,(char)(i+'0')};
			if (tileExists(c)) return false;
		}
		return true;
	}*/
};

struct FtTile : public GtTile<FtTypes, FtTile> {

	inline FtTile(const FtCoordinate& coord_) : GtTile<FtTypes,FtTile>(coord_) {
		// constexpr float R1         = (6378137.0f);
		// geoError = (1.f) / static_cast<float>(1 << coord.level());
		geoError = coord_.geoError();
	}

	inline void createChildren(typename FtTypes::UpdateContext& gtuc) {
		auto &obbMap = gtuc.obbMap;
		for (uint64_t i=0; i<4; i++) {
			uint64_t dx = i % 2;
			uint64_t dy = (i/2) % 2;
			typename FtTypes::Coord c { coord.z()+1, coord.y()*2+dx, coord.x()*2+dy };
			if (obbMap.tileExists(c)) {
				auto t = new typename FtTypes::Tile(c);
				t->bb = obbMap.get(c);
				t->parent = this;
				children.push_back(t);
			}
		}
	}

	bool upload(FtTypes::DecodedCpuTileData& dctd, int idx, GtTileData& td);
	// void updateGlobalBuffer(FtTypes::GlobalBuffer* gpuBuffer);

	void doRender(GtTileData& td, int meshIdx);
	void doRenderCasted(GtTileData& td, const CasterStuff& casterStuff, int meshIdx);

};

struct FtPushConstants {
	static constexpr bool Enabled = false;
	inline FtPushConstants(const FtTile* tile, uint32_t idx) {};
};

struct FtRenderer : public GtRenderer<FtTypes, FtRenderer> {
	// void initPipelinesAndDescriptors(TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);
	void initPipelinesAndDescriptors(const AppConfig& cfg);

	inline FtRenderer(const FtTypes::Config& cfg) : GtRenderer<FtTypes,FtRenderer>(cfg) {}

	virtual ~FtRenderer();

	// Image::Format colorFormat = Image::Format::RGBA;
	int colorChannels = 4;
};

}
