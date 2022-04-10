#pragma once

// #include <Eigen/StdVector>

#include <string>
#include <fstream>
#include <fmt/core.h>
#include <mutex>
#include <thread>
#include <vector>
// #include <unordered_map>
#include <unordered_set>

#include "vk/app.h"
#include "vk/buffer_utils.h"
#include "vk/render_state.h"

#include "utils/eigen.h"

namespace rt {

constexpr static uint32_t NO_INDEX = 999999;

/*
 *
 * Implementation one:
 *		- To get started as fast as possible, just have a draw call per node. You could use indirect buffers, which is preferred, but that is later.
 *
 *		- Per-tile uniform buffers:
 *			+ We'd like to have a per tile uniform buffer. But we can only have so many in a DescSet (15 on my machine).
 *			  That means we need to either:
 *					- 1) Store all such data in push constants
 *					- 2) Have a large combined UBO and index into it.
 *			  I will go with (2) for now.
 *			  However it opens another problem: the tile must know its current index
 *			  This can be done in many ways:
 *					- 1) Using push constants
 *					- 2) Duplicating tile indices into each vertex (e.g. as the 4th byte of the 3-byte position)
 *					- 3) Doing dynamic and/or instanced rendering
 *				I will go with (1) now, which is really the worst solution. (3) Is the best, but I'll get to it later (TODO)
 *
 *
 *
 */

// ^ Pick up from here.


struct RtCfg {
	std::string rootDir;

	static constexpr uint32_t maxTiles = 128;
	// static constexpr uint32_t maxVerts = 2048;
	static constexpr uint32_t maxVerts = (1<<15);
	// static constexpr uint32_t maxInds = 8192;
	static constexpr uint32_t maxInds = 8192 * 8;
	static constexpr uint32_t maxTextureEdge = 512;
	static constexpr uint32_t vertBufferSize() { return maxVerts * 3; }
	// static constexpr vk::Format textureFormat = vk::Format::eR8G8B8A8Unorm;
	static constexpr vk::Format textureFormat = vk::Format::eR8G8B8A8Unorm;
};

struct __attribute__((packed)) RtGlobalData {
	float mvp[16];
	float modelMats[16*RtCfg::maxTiles];
	float uvScalesAndOffs[4*RtCfg::maxTiles];
	// uint32_t drawTileIds[RtCfg::maxTiles];
};

struct RtPushConstants {
	uint32_t index;
};

struct RtDataLoader;
struct RtUpdateContext {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	RtDataLoader& dataLoader;

	// Camera/Screen info
	RowMatrix4f mvp;
	Vector3f eye;
	Vector2f wh;
	float two_tan_half_fov_y;

	float sseThresholdClose, sseThresholdOpen;
};

struct PooledTileData;
struct RtRenderContext {
	const RenderState& rs;
	PooledTileData& pooledTileData;
	vk::raii::CommandBuffer& cmd;
	PipelineStuff& pipelineStuff;
	std::vector<uint32_t> drawTileIds;
};

struct NodeCoordinate {
	static constexpr static uint32_t MAX_LEN = 26;

	inline NodeCoordinate() { memset(key, 0, MAX_LEN); }
	inline NodeCoordinate(const NodeCoordinate& nc) {
		memcpy(key,nc.key, MAX_LEN);
	}
	inline NodeCoordinate(const std::string& s) {
		for (int i=0; i<MAX_LEN; i++) key[i] = i < s.length() ? s[i] : 0;
	}
	inline NodeCoordinate(const char* s) {
		int i = 0;
		for (; i<MAX_LEN; i++) {
			if (s[i] == 0) break;
			key[i] = s[i];
		}
		for (; i<MAX_LEN; i++) key[i] = 0;
	}

	char key[MAX_LEN];

	bool operator==(const NodeCoordinate& b) const {
		for (int i=0; i<MAX_LEN; i++) if (key[i] != b.key[i]) return false;
		return true;
	}
	inline int level() const {
		for (int i=0; i<MAX_LEN; i++) if (key[i] == 0) return i;
		return MAX_LEN;
	}
	struct Hash {
		inline uint64_t operator()(const NodeCoordinate& nc) const {
			uint64_t acc = 0;
			for (int i=0; i<MAX_LEN and nc.key[i] != 0; i++) acc = acc ^ (nc.key[i] * 777lu) + (acc << 3);
			return acc;
		}
	};
};

struct PooledTileData;
class RtTile {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	public:
		RtTile();
		uint32_t resId;
		uint32_t oid;

	public:

	
	inline RtTile(NodeCoordinate& c) : idx(NO_INDEX), nc(c) {
		reset(nc);
	}
	~RtTile();

	NodeCoordinate nc;
	uint32_t idx;
	//ResidentImage* img = nullptr;
	//ResidentBuffer* altBuf = nullptr;
	
	// Either all exist, or all are null
	std::vector<RtTile*> children;

	// The altitude is filled in by sampling the parents'.
	// Contains min/max box (expanded to 8 points of a cube in update())
	Eigen::Matrix<float, 2,3, Eigen::RowMajor> corners;
	float lastSSE = -1;
	float geoError;

	// Needn't be resident after loading the uniform buffer.
	std::vector<float> modelMatf;
	std::vector<float> uvScaleAndOffset;
	

	bool loaded = false;
	enum class MissingStatus {
		UNKNOWN, NOT_MISSING, MISSING
	} childrenMissing = MissingStatus::UNKNOWN;
	bool noData = false;
	bool want_to_close = false;

	// Set bc, initialize corners
	void reset(const NodeCoordinate& nc);

	float computeSSE(const RtUpdateContext& cam);
	inline bool hasChildren() const { return children.size() != 0; }

	void update(const RtUpdateContext& cam, RtTile* parent);
	void render(RtRenderContext& trc);

	// Unload the tile if loaded, then return to pool.
	bool unload(PooledTileData& ptd);
	bool unloadChildren(PooledTileData& ptd);

	enum class State {
		NONE,
		LOADING,
		LOADING_INNER,
		OPENING,
		CLOSING,
		LEAF,
		INNER
	} state = State::NONE;
};

struct __attribute__((packed)) PackedVertex {
	uint8_t x,y,z,w;
	uint16_t u,v;
	uint8_t nx,ny,nz;
	uint8_t extra;
};
static_assert(sizeof(PackedVertex) == 12);
// struct __attribute__((packed)) RtNodeData {
	// float modelMatrix[16];
// };
struct TileData {
	ResidentImage tex;
	ResidentBuffer verts;
	ResidentBuffer inds;
	// Actually: limited amount of ubs in one DescSet, so have just one global UBO and index into it.
	// ResidentBuffer ubo; // Holds RtNodeData
	uint32_t residentVerts, residentInds;
};
// A temporary data type, written to by rt_decode.hpp : todo optimize
struct DecodedTileData {
	alignas(16) double modelMat[16];
	// std::vector<uint8_t> vert_buffer_cpu;
	std::vector<PackedVertex> vert_buffer_cpu;
	std::vector<uint16_t> ind_buffer_cpu;
	std::vector<uint8_t> img_buffer_cpu;
	std::vector<uint8_t> tmp_buffer;
	uint32_t texSize[3];
	float uvOffset[2];
	float uvScale[2];
	float metersPerTexel;
};
struct PooledTileData {
	RtGlobalData rtgd;
	ResidentBuffer globalBuffer;

	std::vector<TileData> datas;
	vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
	vk::raii::DescriptorSet descSet = {nullptr};

	PooledTileData (RtCfg& cfg);

	RtCfg& cfg;

	std::vector<uint32_t> available;

	private:
	public:
		bool withdraw(std::vector<uint32_t>& ids);
		bool deposit(std::vector<uint32_t>& ids);
};

struct RtDataLoader {
	public:
		struct Ask {
			bool isClose = false;
			std::vector<RtTile*> tiles;
			RtTile* parent;
		};
		struct Result {
			bool isClose = false;
			std::vector<RtTile*> tiles;
			RtTile* parent;
		};

		~RtDataLoader();
		inline RtDataLoader(BaseVkApp* app_, RtCfg& cfg_, PooledTileData& p) : app(app_), pooledTileData(p), cfg(cfg_) {}
		bool tileExists(const NodeCoordinate& nc);

		bool loadRootTile(RtTile* tile);
		bool pushAsk(const Ask& ask);

		// Check if any child is not available. Return true if any are not. Can be used from any thread.
		bool childrenAreMissing(const NodeCoordinate& nc);

		void init();
		void shutdown();

	public:
		RtCfg& cfg;
		std::vector<Result> loadedResults;
		std::vector<Ask> asks;
		std::mutex mtx;
		PooledTileData& pooledTileData;
		BaseVkApp* app;

		vk::raii::Queue myUploadQueue  = { nullptr };
		Uploader myUploader;

	private:
		bool loadTile(RtTile* tile);

		bool populated;
		bool populateFromFiles();

		// std::unordered_map<NodeCoordinate, std::string, NodeCoordinate::Hash> nodeToFile;
		// std::unordered_map<NodeCoordinate, std::string, NodeCoordinate::Hash> bulkToFile;
		std::unordered_set<NodeCoordinate, NodeCoordinate::Hash> existingNodes;
		std::unordered_set<NodeCoordinate, NodeCoordinate::Hash> existingBulks;
		std::vector<NodeCoordinate> roots;

		std::thread internalThread;
		void internalLoop();
		bool shouldStop = false;
};


class RtRenderer {
	public:

		inline RtRenderer(RtCfg& cfg_, BaseVkApp* app_) : cfg(cfg_), pooledTileData(cfg), app(app_), dataLoader(app_,cfg,pooledTileData) {}
		~RtRenderer();

		void update(RenderState& rs);
		vk::CommandBuffer render(RenderState& rs);
		vk::CommandBuffer stepAndRender(RenderState& rs);

		void init();

	private:
		RtCfg cfg;
		BaseVkApp* app;
		PipelineStuff pipelineStuff;


		vk::raii::DescriptorPool descPool = {nullptr};
		PooledTileData pooledTileData;
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};

		vk::raii::CommandPool cmdPool { nullptr };
		std::vector<vk::raii::CommandBuffer> cmdBuffers;

		RtTile *root = nullptr;

		RtDataLoader dataLoader;


};



class RocktreeLoader {
	public:
		// RocktreeLoader(const std::string& dir);
		RtTile parseOne(const std::string& path);
	private:
};

}
