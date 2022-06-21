#include "utils/eigen.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <deque>
#include <unistd.h>
#include <fstream>

#include "frastVk/core/app.h"
#include <fmt/color.h>

using Eigen::Matrix4f;
using Eigen::Matrix3f;
using Eigen::Matrix4d;
using Eigen::Matrix3d;
using Eigen::Vector4f;
using Eigen::Vector4d;
using Eigen::Vector3f;
using Eigen::Vector3d;
using Eigen::Quaternionf;

// Max UBO size is 65536. Each tile needs a mat4 and some other stuff probably, so 800 is a good default max.
static constexpr uint32_t GT_NUM_TILES = 800;


// These will be the new buffer types. 'ex' means exclusive, because thread safety must me managed by user.
struct ExBuffer {};
struct ExImage {};

// ObbMap type
// @GtObbMap can give you an Obb given a coordinate. It also is used to see whether children are available or not. It also knows which tiles are roots
// TODO: This will be an lmdb based implementation
// Right now, though, it is in-memory
template <class GtTypes>
class GtObbMap {
	public:
		inline GtObbMap(const std::string& path) : path(path) {
			std::ifstream ifs(path);

			// Decode version 1:
			while (ifs.good()) {
				std::string line;
				std::getline(ifs, line);

				// typename GtTypes::Coord coord = typename GtTypes::Coord::fromString(line);
				typename GtTypes::Coord coord(line);
				float *start = (float*)(line.data() + GtTypes::Coord::Size);
				Vector3f  ctr { start[0], start[1], start[2] };
				Vector3f  ext { start[3], start[4], start[5] };
				Quaternionf q { start[6], start[7], start[8], start[9] };
				map[coord] = typename GtTypes::BoundingBox { ctr, ext, q };
			}

			fmt::print(" - [GtObbMap] loaded {} tile obbs.\n", map.size());

		}

	public:

		std::vector<typename GtTypes::Coord> getRoots() const {
			// TODO
			return {};
		};

		inline bool tileExists(const typename GtTypes::Coord& c) const {
			return map.find(c) != map.end();
		}

		inline const typename GtTypes::BoundingBox& get(const typename GtTypes::Coord& c) const {
			return map[c];
		}

	private:
		std::string path;

		std::unordered_map<typename GtTypes::Coord, typename GtTypes::BoundingBox, typename GtTypes::Coord::Hash> map;
};

// -----------------------------------------------------------
// Render & Update Context Types
// -----------------------------------------------------------

template <class GtTypes>
class GtPooledData;

template <class GtTypes>
struct GtAsk {
	//std::vector<typename GtTypes::Coord> loadCoords;
	//std::vector<uint32_t> loadedIds;
	typename GtTypes::Tile* parent;
	std::vector<typename GtTypes::Tile*> tiles;
	bool isOpen;
};

template <class GtTypes>
struct GtRenderContext {
	const RenderState& rs;
	GtPooledData<GtTypes>& pooledData;
	PipelineStuff& pipelineStuff;
	vk::CommandBuffer& cmd;
};

template <class GtTypes>
struct GtUpdateContext {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	typename GtTypes::DataLoader& dataLoader;

	// Camera/Screen info
	RowMatrix4f mvp;
	Vector3f zplus;
	Vector3f eye;
	Vector2f wh;
	float two_tan_half_fov_y;

	float sseThresholdClose, sseThresholdOpen;
	//int nAsked = 0;
};

/*
// bounding box
// Derived should implement:
//         - float computeSse(gtuc);
template <class GtTypes, class Derived>
struct GtBoundingBox {
	//float computeSse(const GtTypes::UpdateContext& gtuc);
};
*/
template <class GtTypes>
struct GtBoundingBox {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	inline GtBoundingBox() : ctr(Vector3f::Zero()), extents(Vector3f::Zero()), q(Quaternionf::Identity()) {}
	inline GtBoundingBox(const Vector3f& ctr, const Vector3f& ex, const Quaternionf& q) : ctr(ctr), extents(ex), q(q) {}

	float computeSse(const typename GtTypes::UpdateContext& gtuc);

	private:
		Vector3f ctr;
		Vector3f extents;
		Quaternionf q;
};


// GtPooledData has an array of these
struct GtTileData {
	ExImage tex;
	ExImage texOld;
	ExBuffer verts;
	ExBuffer inds;

	bool mustWriteImageDesc = false;
	uint32_t residentInds=0, residentVerts=0;
};

// Holds all GPU buffers, list of free tile buffers, and descriptor stuff
template <class GtTypes>
struct GtPooledData {
	typename GtTypes::GlobalBuffer gtgb;
	ExBuffer globalBuffer;

	std::vector<GtTileData> datas;
	vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
	vk::raii::DescriptorSet descSet = {nullptr};

	std::mutex availableMtx;
	std::vector<uint32_t> available;
	int n_available_;

	GtPooledData();
	inline int nAvailable() const { return n_available_; }
	bool withdraw(std::vector<uint32_t>& ids, bool isClose);
	bool deposit(const std::vector<uint32_t>& ids);
};

// Note: A tile may consist of more than one mesh!
// Derived should implement:
//
template <class GtTypes, class Derived>
struct GtTile {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	Matrix4f model;
	typename GtTypes::BoundingBox bb;
	typename GtTypes::Coord coord;
	std::vector<uint32_t> residentIds;

	std::vector<uint32_t> meshIds;

	enum class State {
		NONE,
		LOADING,
		LOADING_INNER,
		OPENING,
		CLOSING,
		LEAF,
		INNER
	} state = State::NONE;

	float lastSSE = -1;
	float geoError;
	bool noData = false;
	bool want_to_close = false;
	bool loaded = false;
	uint8_t mask = 0;

	std::vector<Derived*> children;
};

// Derived should implement:
//	          loadTile()
template <class GtTypes, class Derived>
struct GtDataLoader {

	inline GtDataLoader(typename GtTypes::Renderer& renderer_) : renderer(renderer_) {
		thread = std::thread(&GtDataLoader::internalLoop, this);
	}

	void pushAsk(GtAsk<GtTypes>& ask);

	private:
		void internalLoop();
		std::thread thread;
		typename GtTypes::Renderer& renderer;

		std::condition_variable cv;
		std::mutex mtx;
		std::deque<GtAsk<GtTypes>> asks;
		bool doStop = false;
};

template <class GtTypes, class Derived>
struct GtRenderer {

	public:
		inline GtRenderer()
			: loader((Derived&)*this),
			  obbMap("/data/gearth/tpAois_wgs/index.v1.bin")
		{}

	public:
		GtPooledData<GtTypes> gtpd;
		typename GtTypes::DataLoader loader;
		GtObbMap<GtTypes> obbMap;
		std::vector<typename GtTypes::Tile*> roots;

		std::mutex askMtx;
		// Called from GtDataLoader
		void pushResult(GtAsk<GtTypes>& ask); // Note: will move
		std::vector<GtAsk<GtTypes>> waitingResults;
		// Must be called from render thread
		void update(GtUpdateContext<GtTypes>& gtuc);


		void render(RenderState& rs, vk::CommandBuffer& cmd);

		PipelineStuff pipelineStuff;


};

// -----------------------------------------------------------
// Render & Update Context Types
// -----------------------------------------------------------


// -----------------------------------------------------------
// Gt Impl
// -----------------------------------------------------------

////////////////////////////
// GtRenderer
////////////////////////////

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::pushResult(GtAsk<GtTypes>& ask) {
	std::lock_guard<std::mutex> lck(askMtx);
	waitingResults.push_back(ask);
}

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::update(GtUpdateContext<GtTypes>& gtuc) {
	// TODO NO NO NO
	// 1) Traverse tree, compute sse, update state, make any queries as needed
	// 2) Handle loaded results
	// TODO:
	// Decide whether to have third thread that just computes sse
	// Decide whether to have a map of OBBs seperate from tile data in files

	decltype(waitingResults) theResults;
	{
		std::lock_guard<std::mutex> lck(askMtx);
		theResults = std::move(waitingResults);
		waitingResults.clear();
	}

	if (theResults.size()) {
		fmt::print(fmt::fg(fmt::color::yellow), " - [#update] handling {} loaded results\n", theResults.size());
	}

}

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::render(RenderState& rs, vk::CommandBuffer& cmd) {
	PipelineStuff* thePipelineStuff = nullptr;
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 1, {1,&*gtpd.descSet}, nullptr);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	thePipelineStuff = &pipelineStuff;

}

////////////////////////////
// GtPooledData
////////////////////////////

template <class GtTypes>
GtPooledData<GtTypes>::GtPooledData() {
	available.resize(GT_NUM_TILES);
	for (int i=0; i<GT_NUM_TILES; i++) available[i] = i;
	n_available_ = GT_NUM_TILES;
}
template <class GtTypes>
bool GtPooledData<GtTypes>::withdraw(std::vector<uint32_t>& ids, bool isClose) {
	std::lock_guard<std::mutex> lck(availableMtx);

	assert(available.size() >= ids.size());

	// TODO close/open logic: fail if not close and too few.
	for (int i=0; i<ids.size(); i++) {
		ids[i] = available.back();
		available.pop_back();
	}

	return false;
}
template <class GtTypes>
bool GtPooledData<GtTypes>::deposit(const std::vector<uint32_t>& ids) {
	std::lock_guard<std::mutex> lck(availableMtx);

	for (int i=0; i<ids.size(); i++) {
		assert(ids[i] < GT_NUM_TILES);
		available.push_back(ids[i]);
		assert(available.size() <= GT_NUM_TILES);
	}

	return false;
}

////////////////////////////
// GtDataLoader
////////////////////////////

template <class GtTypes, class Derived>
void GtDataLoader<GtTypes,Derived>::pushAsk(GtAsk<GtTypes>& ask) {
	std::lock_guard<std::mutex> lck(mtx);
	asks.push_back(std::move(ask));
}

template <class GtTypes, class Derived>
void GtDataLoader<GtTypes,Derived>::internalLoop() {
	std::cout << " - GtDataLoader starting loader thread." << std::endl;

	while (true) {
		std::unique_lock<std::mutex> lck(mtx);
		cv.wait(lck, [this]() { return doStop or asks.size(); });

		if (doStop) break;

		// Move the @asks member to a temporary buffer, and empty it.
		decltype(asks) curAsks;
		{
			lck.lock();
			curAsks = std::move(asks);
			asks.clear();
			lck.unlock();
		}

		while (curAsks.size()) {
			auto &ask = curAsks.back();

			// We do this in two phases, because we don't know the number of meshes apriori
			// So
			//    1) Load all data to disk, in whatever specific format (But @DecodedCpuTileData must have a '@meshes' vector member)
			//    2) Withdraw tiles from @available
			//    3) Upload the decoded data

			std::vector<typename GtTypes::DecodedCpuTileData> decodedTileDatas(ask.tiles.size());

			int total_meshes = 0;
			for (int i=0; i<ask.tiles.size(); i++) {
				auto &tile = ask.tiles[i];
				int n_meshes = static_cast<Derived*>(this)->loadTile(tile, decodedTileDatas[i]);
				total_meshes += n_meshes;
			}

			std::vector<uint32_t> gatheredIds(total_meshes);
			renderer.gtpd.withdraw(gatheredIds, !ask.isOpen);


			int mesh_ii = 0;
			for (int i=0; i<ask.tiles.size(); i++) {

				auto &tile = ask.tiles[i];
				auto &coord = tile->coord;

				for (auto &meshData : decodedTileDatas[i].meshes) {
					uint32_t idx = gatheredIds[mesh_ii];
					tile->meshIds.push_back(idx);
					auto error = static_cast<Derived*>(this)->uploadTile(tile, meshData, renderer.gtpd.datas[idx]);
				}
			}

			renderer.pushResult(ask);

			curAsks.pop_back();
		}

		// TODO: If any asks failed, copy them back to the @asks member

	}
};

////////////////////////////
// GtBoundingBox
////////////////////////////

template <class GtTypes>
float GtBoundingBox<GtTypes>::computeSse(const typename GtTypes::UpdateContext& gtuc) {
	return 0.f;
}

// -----------------------------------------------------------
// Should be rt.cc
// -----------------------------------------------------------

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

struct RtTile;
struct RtDataLoader;
struct RtCoordinate;
struct RtRenderer;
struct RtBoundingBox;
struct RtTypes {
	using Tile = RtTile;
	using DataLoader = RtDataLoader;
	using Coord = RtCoordinate;
	using Renderer = RtRenderer;
	//using BoundingBox = RtBoundingBox;

	// Defaults
	using BoundingBox = GtBoundingBox<RtTypes>;
	using UpdateContext = GtUpdateContext<RtTypes>;


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

};

/*
struct RtBoundingBox : public GtBoundingBox<RtTypes, RtBoundingBox> {
	inline float computeSse(const GtUpdateContext<RtTypes>& gtuc) const {
		return 0.f;
	}
};
*/

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
		for (int i=0; i<MAX_LEN; i++) key[i] = i < s.length() ? s[i] : 0;
		len = s.length();
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

	// Unfortunately, we need an adaptor to the generic constructor :(
	inline RtDataLoader(typename RtTypes::Renderer& renderer_) : GtDataLoader<RtTypes, RtDataLoader>(renderer_) {
	}

	inline int loadTile(RtTile* tile, RtTypes::DecodedCpuTileData& td) {
		return 0;
	}
	inline bool uploadTile(RtTile* tile, RtTypes::DecodedCpuTileData::MeshData& dctd, GtTileData& td) {
		return false;
	}

};

struct RtTile : public GtTile<RtTypes, RtTile> {
};

struct RtRenderer : public GtRenderer<RtTypes, RtRenderer> {
};


int main() {

	RtRenderer renderer;

	sleep(10);


	return 0;
}
