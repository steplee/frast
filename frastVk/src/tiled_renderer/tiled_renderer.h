#pragma once

#include <mutex>
#include <thread>

#include <frast/db.h>

#include "core/render_state.h"
#include "core/app.h"
#include "utils/eigen.h"

namespace tr1 {

constexpr static uint32_t NO_INDEX = 999999;
class Tile;

struct TiledRendererPushConstants {
	bool grayscale = false;
	bool pad[3];
};

struct TiledRendererCfg {
	std::string colorPath, dtedPath;
	inline TiledRendererCfg(const std::string &colorPath,
		const std::string &dtedPath) : colorPath(colorPath), dtedPath(dtedPath) {}

	//static constexpr uint32_t maxTiles = 128;
	static constexpr uint32_t maxTiles = 256;
	uint32_t vertsAlongEdge = 8;
	uint32_t tileSize = 256;
	uint32_t channels = 3; // Must be 1, 3, or 4. If 3, then textures actually have 4 channels.
};

struct SharedTileData {
	//vk::raii::Buffer vertsXY = {nullptr};
	//vk::raii::Buffer inds = {nullptr};
	ResidentBuffer vertsXYI;
	ResidentBuffer inds;
	PipelineStuff pipelineStuff;
	uint32_t numInds = 0;
};

/*struct ResidentTile {
	vk::raii::Image colorTex = {nullptr};
	vk::raii::Buffer altBuf = {nullptr};
}; */
struct PooledTileData {
	//std::vector<Tile> tiles;
	std::vector<ResidentImage> texs;
	std::vector<ResidentBuffer> altBufs;
	vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
	vk::raii::DescriptorSet descSet = {nullptr};

	PooledTileData (TiledRendererCfg& cfg);

		TiledRendererCfg& cfg;
	private:
		std::vector<uint32_t> available;
	public:
		bool withdrawOne(uint32_t out[1]);
		bool withdrawFour(uint32_t out[4]);
		bool depositOne(uint32_t in[1]);
		bool depositFour(uint32_t in[4]);
};


struct __attribute__((packed)) AltBuffer {
	uint32_t x,y,z, pad;
	float alt[64];
};

struct TileDataLoader {
	public:
		struct Ask {
			int ntiles = 0;
			Tile* tiles[4];
			Tile* parent;
		};
		struct Result {
			int ntiles = 0;
			Tile* tiles[4];
			// Rather than traversing every time we handle a result, just store the parent.
			Tile* parent;
		};

		~TileDataLoader();
		inline TileDataLoader(BaseVkApp* app, PooledTileData& p) : app(app), pooledTileData(p), cfg(p.cfg) {}
		bool tileExists(const BlockCoordinate& bc);

		bool loadRootTile(Tile* tile);
		bool pushAsk(const Ask& ask);

		// Check if any child is not available. Return true if any are not. Can be used from any thread.
		bool childrenAreMissing(const BlockCoordinate& bc);

		void init(const std::string& colorDsetPath, const std::string& elevDsetPath);

	public:
		TiledRendererCfg& cfg;
		std::vector<Result> loadedResults;
		std::vector<Ask> asks;
		std::mutex mtx;
		PooledTileData& pooledTileData;
		BaseVkApp* app;
		Image::Format colorFormat;

		vk::raii::Queue myUploadQueue  = { nullptr };
		Uploader myUploader;


	private:
		MDB_txn* color_txn = nullptr;
		MDB_txn* elev_txn = nullptr;
		bool loadTile(Tile* tile);

		DatasetReader* colorDset = nullptr;
		DatasetReader* elevDset  = nullptr;

		Image colorBuf;
		Image elevBuf;
		AltBuffer altBuffer;


		bool loadColor(Tile* tile);
		bool loadElevAndMeta(Tile* tile);

		std::thread internalThread;
		void internalLoop(const std::string& colorDsetPath, const std::string& elevDsetPath);
		bool shouldStop = false;
};

/*
struct __attribute__((packed, aligned(8))) TRPushConstants {
	uint32_t z, y, x;
};
*/

struct TiledRenderer {
		TiledRenderer(TiledRendererCfg& cfg, BaseVkApp* app);

		void update(const RenderState& rs);
		vk::CommandBuffer render(const RenderState& rs);
		vk::CommandBuffer stepAndRender(const RenderState& rs);

		void init();


	private:
		BaseVkApp* app;
		Tile* root = nullptr;
		TiledRendererCfg cfg;


		vk::raii::DescriptorPool descPool = {nullptr};
		SharedTileData sharedTileData;
		PooledTileData pooledTileData;
		ResidentBuffer globalBuffer;
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};

		vk::raii::CommandPool cmdPool { nullptr };
		std::vector<vk::raii::CommandBuffer> cmdBuffers;

		TileDataLoader dataLoader;
};

struct __attribute__((packed)) TRGlobalData {
	float mvp[16];
	uint32_t drawTileIds[TiledRendererCfg::maxTiles];
};



struct TileUpdateContext {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	TileDataLoader& dataLoader;


	// Camera/Screen info
	RowMatrix4f mvp;
	Vector3f eye;
	Vector2f wh;
	float two_tan_half_fov_y;

	float sseThresholdClose, sseThresholdOpen;
};

struct TileRenderContext {
	const RenderState& rs;
	vk::raii::CommandBuffer& cmd;
	SharedTileData& std;
	uint32_t numInds;
	std::vector<uint32_t> drawTileIds;
};

struct Tile {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	
	inline Tile() : idx(NO_INDEX), bc(0) {}
	inline Tile(BlockCoordinate& bc) : idx(NO_INDEX), bc(0) {
		reset(bc);
	}
	~Tile();

	BlockCoordinate bc;
	uint32_t idx;
	//ResidentImage* img = nullptr;
	//ResidentBuffer* altBuf = nullptr;
	
	// Either all exist, or all are null
	Tile* children[4] = {nullptr};

	// The altitude is filled in by sampling the parents'.
	// Contains min/max box (expanded to 8 points of a cube in update())
	Eigen::Matrix<float, 2,3, Eigen::RowMajor> corners;
	float lastSSE = -1;
	

	bool loaded = false;
	enum class MissingStatus {
		UNKNOWN, NOT_MISSING, MISSING
	} childrenMissing = MissingStatus::UNKNOWN;
	//bool wants_to_close = false;
	//bool wants_to_open = false;
	//bool opening = false;

	// Set bc, initialize corners
	void reset(const BlockCoordinate& bc);

	float computeSSE(const TileUpdateContext& cam);
	inline bool hasChildren() const { return children[0] != nullptr; }

	void update(const TileUpdateContext& cam, Tile* tile);
	void render(TileRenderContext& trc);

	// Unload the tile if loaded, then return to pool.
	bool unload(PooledTileData& ptd);

	enum class State {
		NONE,
		LOADING,
		LOADING_INNER,
		LEAF,
		INNER,
		OPENING,
		CLOSING
	} state = State::NONE;
};

}
