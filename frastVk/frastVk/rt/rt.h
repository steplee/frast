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

#include "frastVk/core/app.h"
#include "frastVk/core/buffer_utils.h"
#include "frastVk/core/render_state.h"
#include "frastVk/extra/caster/castable.h"

// #include "frastVk/utils/eigen.h"

#include <frast/image.h>


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
 */

// ^ Pick up from here.


struct RtCfg {
	std::string rootDir;

	// This must match the number of array elements in the shaders.
	static constexpr uint32_t maxTiles = 800;
	// Don't allow opening upto 8 children if we are close to hitting max tiles.
	// Helps to prevent situation where we are deadlocked, not being to make any changes to tree.
	static constexpr uint32_t tilesDedicatedToClosing = 16;
	// static constexpr uint32_t maxVerts = 2048;
	static constexpr uint32_t maxVerts = (1<<13);
	// static constexpr uint32_t maxInds = 8192;
	static constexpr uint32_t maxInds = 8192 * 2;
	static constexpr uint32_t maxTextureEdge = 256;
	static constexpr uint32_t vertBufferSize() { return maxVerts * 3; }
	// static constexpr vk::Format textureFormat = vk::Format::eR8G8B8A8Unorm;
	static constexpr vk::Format textureFormat = vk::Format::eR8G8B8A8Unorm;

	float sseThresholdOpen = 1.99;
	float sseThresholdClose = .9;
	bool dbg = false;
};

struct __attribute__((packed)) RtGlobalData {
	float mvp[16];
	float offset[4];
	float modelMats[16*RtCfg::maxTiles];
	float uvScalesAndOffs[4*RtCfg::maxTiles];
	// uint32_t drawTileIds[RtCfg::maxTiles];
};

struct RtPushConstants {
	uint32_t index;
	uint32_t octantMask;
	uint32_t level;
};

struct RtDataLoader;
struct RtUpdateContext {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	RtDataLoader& dataLoader;

	// Camera/Screen info
	RowMatrix4f mvp;
	Vector3f zplus;
	Vector3f eye;
	Vector2f wh;
	float two_tan_half_fov_y;

	float sseThresholdClose, sseThresholdOpen;
};

struct PooledTileData;
struct RtRenderContext {
	const RenderState& rs;
	PooledTileData& pooledTileData;
	PipelineStuff& pipelineStuff;
	std::vector<uint32_t> drawTileIds;

	vk::CommandBuffer& cmd;
};



struct NodeCoordinate {
	static constexpr static uint32_t MAX_LEN = 26;

	inline NodeCoordinate() { memset(key, 0, MAX_LEN); }
	inline NodeCoordinate(const NodeCoordinate& nc) {
		memcpy(key,nc.key, MAX_LEN);
		len = nc.len;
	}
	inline NodeCoordinate(const NodeCoordinate& nc, char next) {
		memcpy(key,nc.key, MAX_LEN);
		len = nc.len + 1;
		key[nc.len] = next;
	}
	inline NodeCoordinate(const std::string& s) {
		for (int i=0; i<MAX_LEN; i++) key[i] = i < s.length() ? s[i] : 0;
		len = s.length();
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
	uint8_t len=0;

	bool operator==(const NodeCoordinate& b) const {
		if (b.len != len) return false;
		for (int i=0; i<len; i++) if (key[i] != b.key[i]) return false;
		return true;
	}
	inline int level() const {
		return len;
	}
	struct Hash {
		inline uint64_t operator()(const NodeCoordinate& nc) const {
			uint64_t acc = 0;
			for (int i=0; i<nc.len and nc.key[i] != 0; i++) acc = acc ^ (nc.key[i] * 777lu) + (acc << 3);
			return acc;
		}
	};
};

struct RtMesh {
	uint32_t idx = NO_INDEX;
	std::vector<float> uvScaleAndOffset;
};

struct PooledTileData;
class RtTile {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	public:
		RtTile();

	public:

	
	inline RtTile(NodeCoordinate& c) : nc(c) {
		reset(nc);
	}
	~RtTile();

	NodeCoordinate nc;
	
	std::vector<RtTile*> children;

	// The altitude is filled in by sampling the parents'.
	// Contains min/max box (expanded to 8 points of a cube in update())
	Eigen::Matrix<float, 2,3, Eigen::RowMajor> corners;
	float lastSSE = -1;
	float geoError;

	// Needn't be resident after loading the uniform buffer.
	std::vector<float> modelMatf;

	bool loaded = false;
	std::vector<RtMesh> meshes;

	enum class MissingStatus {
		UNKNOWN, NOT_MISSING, MISSING
	} childrenMissing = MissingStatus::UNKNOWN;
	bool noData = false;
	bool want_to_close = false;
	uint8_t mask = 0;

	// Set bc, initialize corners
	void reset(const NodeCoordinate& nc);

	float computeSSE(const RtUpdateContext& cam);
	inline bool hasChildren() const { return children.size() != 0; }

	void update(const RtUpdateContext& cam, RtTile* parent);
	void render(RtRenderContext& trc);
	void renderDbg(RtRenderContext& trc);

	// Unload the tile if loaded, then return to pool.
	bool unload(PooledTileData& ptd, BaseVkApp* app);
	bool unloadChildren(PooledTileData& ptd, BaseVkApp* app);

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
struct TileData {
	ResidentImage tex;
	ResidentImage texOld;
	ResidentBuffer verts;
	ResidentBuffer inds;
	// Actually: limited amount of ubs in one DescSet, so have just one global UBO and index into it.
	// ResidentBuffer ubo; // Holds RtNodeData
	uint32_t residentVerts, residentInds;
	bool mustWriteImageDesc = false;
};
// };
// A temporary data type, written to by rt_decode.hpp : todo optimize
struct DecodedTileData {
	alignas(16) double modelMat[16];
	// std::vector<uint8_t> vert_buffer_cpu;
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
struct PooledTileData {
	RtGlobalData rtgd;
	ResidentBuffer globalBuffer;
	std::vector<TileData> datas;
	// std::vector<TileData> datas;
	vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
	vk::raii::DescriptorSet descSet = {nullptr};

	PooledTileData (RtCfg& cfg);

	RtCfg& cfg;
	std::vector<uint32_t> available;
	int n_available = RtCfg::maxTiles;

	public:
		bool withdraw(std::vector<uint32_t>& ids, bool isClose);
		bool deposit(std::vector<uint32_t>& ids);
	public:
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
		enum class LoadStatus {
			eSuccess,
			eTryAgain,
			eFailed
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

		// See if we should allow pushAsk with open queries. This is false
		// if near maxTiles.
		bool allowOpenAsks();

	public:
		RtCfg& cfg;
		std::vector<Result> loadedResults;
		std::vector<Ask> asks;
		std::mutex mtx;
		PooledTileData& pooledTileData;
		BaseVkApp* app;

		vk::raii::Queue myUploadQueue  = { nullptr };
		Uploader myUploader;
		uint64_t sleepMicros = 63'000;

	private:
		LoadStatus loadTile(RtTile* tile, bool isClose);

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


class RtRenderer : public Castable
{
	public:

		inline RtRenderer(RtCfg& cfg_, BaseVkApp* app_) : cfg(cfg_), pooledTileData(cfg), app(app_), dataLoader(app_,cfg,pooledTileData) {}
		~RtRenderer();

		void update(RenderState& rs);
		void render(RenderState& rs, vk::CommandBuffer&);
		void stepAndRender(RenderState& rs, vk::CommandBuffer&);
		inline void setDataLoaderSleepMicros(int64_t t) { dataLoader.sleepMicros = t; }

		void init();

		bool allowUpdate = true;

		// inline void setCasterInRenderThread(CasterWaitingData& cwd) { Castable::setCasterInRenderThread(cwd,app); }

	private:
		RtCfg cfg;
		BaseVkApp* app;

		PipelineStuff pipelineStuff;

		// Debug render wireframe bboxes of active tiles
		PipelineStuff dbgPipelineStuff;


		vk::raii::DescriptorPool descPool = {nullptr};
		PooledTileData pooledTileData;
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};

		vk::raii::CommandPool cmdPool { nullptr };
		std::vector<vk::raii::CommandBuffer> cmdBuffers;

		RtTile *root = nullptr;

		RtDataLoader dataLoader;

		void init_caster_stuff();


};



class RocktreeLoader {
	public:
		// RocktreeLoader(const std::string& dir);
		RtTile parseOne(const std::string& path);
	private:
};

}
