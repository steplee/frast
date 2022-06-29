#pragma once

#include "utils/eigen.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <deque>
#include <unistd.h>
#include <fstream>
#include <stack>

// #include "frastVk/core/app.h"
#include "frastVk/core/fvkApi.h"

#include <fmt/color.h>
#include <fmt/ostream.h>

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


// ObbMap type
// @GtObbMap can give you an Obb given a coordinate. It also is used to see whether children are available or not. It also knows which tiles are roots
// TODO: This will be an lmdb based implementation. Right now, though, it is in-memory
// Derived must implement:
//		bool tileIsTerminal()
template <class GtTypes, class Derived>
class GtObbMap {
	public:
		inline GtObbMap(const std::string& path) : path(path) {
			std::ifstream ifs(path);

			assert(ifs.good());

			fmt::print(" - [GtObbMap] loading obbs.\n");

			auto row_size = 26 + 4*(3+3+4);

			//std::vector<uint8_t> buffer(row_size);
			std::string buffer(row_size, '\0');

			// Decode version 1:
			while (ifs.good()) {
				ifs.read(buffer.data(), row_size);

				// typename GtTypes::Coord coord = typename GtTypes::Coord::fromString(line);
				typename GtTypes::Coord coord(buffer);
				float *start = (float*)(buffer.data() + GtTypes::Coord::Size);
				Vector3f  ctr { start[0], start[1], start[2] };
				Vector3f  ext { start[3], start[4], start[5] };
				Quaternionf q { start[6], start[7], start[8], start[9] };
				map[coord] = typename GtTypes::BoundingBox { ctr, ext, q };
			}

			fmt::print(" - [GtObbMap] loaded {} tile obbs.\n", map.size());

			// Find roots
			for (auto& kv : map) {
				typename GtTypes::Coord parentKey = kv.first.parent();
				//fmt::print(" - key {}\n", kv.first.toString());
				//fmt::print(" - par {}\n", parentKey.toString());
				if (map.find(parentKey) == map.end()) roots.push_back(kv.first);
			}

			fmt::print(" - [GtObbMap] found {} roots\n", roots.size());
		}

	public:

		std::vector<typename GtTypes::Coord>& getRootCoords() {
			// TODO
			return roots;
		};

		inline bool tileExists(const typename GtTypes::Coord& c) const {
			return map.find(c) != map.end();
		}

		inline const typename GtTypes::BoundingBox& get(const typename GtTypes::Coord& c) {
			return map[c];
		}

	private:
		std::string path;
		std::vector<typename GtTypes::Coord> roots;

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
	typename GtTypes::Tile* ancestor = nullptr;
	std::vector<typename GtTypes::Tile*> tiles;
	bool isOpen;
};

template <class GtTypes>
struct GtRenderContext {
	const RenderState& rs;
	GtPooledData<GtTypes>& pooledData;
	// PipelineStuff& pipelineStuff;
	GraphicsPipeline& gfxPipeline;

	// vk::CommandBuffer& cmd;
	VkCommandBuffer& cmd;
	std::vector<uint32_t> drawIds;
	uint32_t drawCount = 0;
};

template <class GtTypes>
struct GtUpdateContext {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	typename GtTypes::DataLoader& dataLoader;
	typename GtTypes::ObbMap& obbMap;
	GtPooledData<GtTypes>& pooledData;

	// Camera/Screen info
	RowMatrix4f mvp;
	Vector3f zplus;
	Vector3f eye;
	Vector2f wh;
	float two_tan_half_fov_y;

	float sseThresholdClose, sseThresholdOpen;

	GtAsk<GtTypes> currentOpenAsk;
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

	float computeSse(const typename GtTypes::UpdateContext& gtuc) const;

	public:
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
	// typename GtTypes::GlobalBuffer gtgb;
	ExBuffer globalBuffer;
	DescriptorSet globalDescSet;

	std::vector<GtTileData> datas;
	// vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
	// vk::raii::DescriptorSet descSet = {nullptr};
	DescriptorSet tileDescSet;

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
//            createChildren(gtuc)
//            render()
template <class GtTypes, class Derived>
struct GtTile {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	inline GtTile(const typename GtTypes::Coord& c) : coord(c) {}

	Derived* parent = nullptr;
	Matrix4f model;
	typename GtTypes::BoundingBox bb;
	typename GtTypes::Coord coord;
	std::vector<uint32_t> residentIds;

	std::vector<uint32_t> meshIds;

	/*enum class State {
		NONE,
		LOADING,
		LOADING_INNER,
		OPENING,
		CLOSING,
		LEAF,
		INNER
	} state = State::NONE;*/

	float lastSSE = -1;
	float geoError = 1;
	bool noData = false;
	bool want_to_close = false;
	bool loaded = false;
	uint8_t mask = 0;
	void unload(typename GtTypes::UpdateContext& gtuc);

	std::vector<Derived*> children;

	enum class UpdateStatus {
		eClose, eOpen, eNone
	};

	// If @sseIsCached, assume parent called computeSse and stored to lastSSE
	UpdateStatus update(typename GtTypes::UpdateContext& gtuc, bool sseIsCached=false);
	void render(typename GtTypes::RenderContext& gtrc);

	public:
		inline bool terminal() const { return flags & TERMINAL; }
		inline bool closing() const { return flags & CLOSING; }
		inline bool opening() const { return flags & OPENING; }
		inline bool openingAsLeaf() const { return flags & OPENING_AS_LEAF; }
		inline bool closingChildren() const { return flags & CLOSING_CHILDREN; }
		inline bool root() const { return flags & ROOT; }

		inline bool leaf() const { return state & LEAF; }
		inline bool inner() const { return state & INNER; }
		inline bool invalid() const { return state & INVALID; }

	//private:
		static constexpr uint8_t CLOSING=1, OPENING=2, ROOT=4, TERMINAL=8, OPENING_AS_LEAF=16, CLOSING_CHILDREN=32;
		static constexpr uint8_t LEAF=1, INNER=2, INVALID=4;
		uint8_t flags=0;
		uint8_t state=INVALID;

		inline std::string toString() const {
			char buf[128];
			std::string cs = coord.toString();
			std::string fs = "";
			if (flags & CLOSING) fs += " closing";
			if (flags & OPENING) fs += " opening";
			if (flags & ROOT) fs += " root";
			if (flags & TERMINAL) fs += " terminal";
			if (flags & OPENING_AS_LEAF) fs += " openAsLeaf";
			if (flags & CLOSING_CHILDREN) fs += " closingChildren";
			sprintf(buf, "(t %s) (s %s) (flags%s) (sse %f)", cs.c_str(), state==LEAF?"leaf":state==INVALID?"invalid":"inner", fs.c_str(), lastSSE);
			return std::string{buf};
		}

};

// Derived should implement:
//	          loadTile()
template <class GtTypes, class Derived>
struct GtDataLoader {

	inline GtDataLoader(typename GtTypes::Renderer& renderer_) : renderer(renderer_) {
	}

	void pushAsk(GtAsk<GtTypes>& ask);

	void init(Device& d, uint32_t newQueueNumber=1);

	inline void join() {
		mtx.lock();
		doStop = true;
		mtx.unlock();
		cv.notify_one();
		fmt::print(" - [GtDataLoader] joining...\n");
		thread.join();
		fmt::print(" - [GtDataLoader] ...joined\n");
	}

	protected:
		void internalLoop();
		std::thread thread;
		typename GtTypes::Renderer& renderer;

		std::condition_variable cv;
		std::mutex mtx;
		std::deque<GtAsk<GtTypes>> asks;
		bool doStop = false;

		ExUploader uploader;
		Queue uploaderQueue;
};

// Derived must implement:
//		bool initPipelinesAndDescriptors()
template <class GtTypes, class Derived>
struct GtRenderer {

	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		inline GtRenderer()
			: loader((Derived&)*this),
			  obbMap(new typename GtTypes::ObbMap("/data/gearth/tpAois_wgs/index.v1.bin"))
		{

			GtAsk<GtTypes> ask;
			ask.isOpen = true;
			ask.ancestor = nullptr;
			for (auto &rootCoord : obbMap->getRootCoords()) {
				auto root = new typename GtTypes::Tile(rootCoord);
				root->bb = obbMap->get(rootCoord);
				fmt::print(" - root OBB: {} | {} | {}\n", root->bb.ctr.transpose(), root->bb.extents.transpose(), root->bb.q.coeffs().transpose());
				root->state = GtTypes::Tile::INVALID;
				root->flags = GtTypes::Tile::ROOT | GtTypes::Tile::OPENING_AS_LEAF;
				roots.push_back(root);
				ask.tiles.push_back(root);
			}
			loader.pushAsk(ask);
		}

		inline ~GtRenderer() {
			loader.join();
			delete obbMap;
			sampler.destroy(*device);
		}

	protected:
		Device *device { nullptr };
	public:

		void init(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, const AppConfig& cfg);

		GtPooledData<GtTypes> gtpd;
		typename GtTypes::DataLoader loader;
		// GtObbMap<GtTypes> obbMap;
		typename GtTypes::ObbMap* obbMap = nullptr;
		std::vector<typename GtTypes::Tile*> roots;

		std::mutex askMtx;
		// Called from GtDataLoader
		void pushResult(GtAsk<GtTypes>& ask); // Note: will move
		std::vector<GtAsk<GtTypes>> waitingResults;
		// Must be called from render thread
		void update(GtUpdateContext<GtTypes>& gtuc);

		void render(RenderState& rs, Command& cmd);

		GraphicsPipeline gfxPipeline;

		Sampler sampler;

		inline uint32_t activeTiles() { return GT_NUM_TILES - gtpd.available.size(); }
};

