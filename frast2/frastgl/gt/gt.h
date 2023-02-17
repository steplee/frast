#pragma once

#include "frast2/frastgl/utils/eigen.h"
#include "frast2/frastgl/core/render_state.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <deque>
#include <unistd.h>
#include <fstream>
#include <stack>

// #include "frastVk/core/app.h"
// #include "frastVk/core/fvkApi.h"
// #include "frastVk/extra/caster/castable.h"
#include "frast2/frastgl/extra/caster/castable.h"

#include <fmt/color.h>
#include <fmt/ostream.h>

#include "frast2/frastgl/core/shader.h"

namespace frast {

struct AppConfig;

//
// TODO: Allow a @sharedInds member that avoids having seperate index buffers when not needed
//
// TODO: Make ftr more like rt: use model matrices and uint8_t vertices instead of float32 verts and no model matrix
//

using Eigen::Matrix4f;
using Eigen::Matrix3f;
using Eigen::Matrix4d;
using Eigen::Matrix3d;
using Eigen::Vector4f;
using Eigen::Vector4d;
using Eigen::Vector3f;
using Eigen::Vector3d;
using Eigen::Array3f;
using Eigen::Quaternionf;
using RowMatrix83f = Eigen::Matrix<float,8,3,Eigen::RowMajor>;
using RowMatrix84f = Eigen::Matrix<float,8,4,Eigen::RowMajor>;

// Max UBO size is 65536. Each tile needs a mat4 and some other stuff probably, so 800 is a good default max.
static constexpr uint32_t GT_NUM_TILES = 800;
static constexpr bool GT_DEBUG = true;



// ObbMap type
// @GtObbMap can give you an Obb given a coordinate. It also is used to see whether children are available or not. It also knows which tiles are roots
// TODO: This will be an lmdb based implementation. Right now, though, it is in-memory
// Derived must implement:
//		bool tileIsTerminal() -- nevermind, dont need this
template <class GtTypes, class Derived>
class GtObbMap {
	public:
		inline GtObbMap(const std::vector<std::string>& paths) : paths(paths) {

			// fmt::print(" - [GtObbMap] loading obbs.\n");

			auto row_size = (GtTypes::Coord::SerializedSize) + 4*(3+3+4);

			//std::vector<uint8_t> buffer(row_size);
			std::string buffer(row_size, '\0');

			// Decode version 1:
			for (const auto& path : paths) {
				std::ifstream ifs(path);
				assert(ifs.good());
				while (ifs.good()) {
					ifs.read(buffer.data(), row_size);

					// typename GtTypes::Coord coord = typename GtTypes::Coord::fromString(line);
					typename GtTypes::Coord coord(buffer);
					float *start = (float*)(buffer.data() + GtTypes::Coord::SerializedSize);
					Vector3f  ctr { start[0], start[1], start[2] };
					Vector3f  ext { start[3], start[4], start[5] };
					Quaternionf q { start[6], start[7], start[8], start[9] };
					map[coord] = typename GtTypes::BoundingBox { ctr, ext, q };
				}
			}

			fmt::print(" - [GtObbMap] loaded {} tile obbs.\n", map.size());

			// Find roots
			for (auto& kv : map) {
				typename GtTypes::Coord parentKey = kv.first.parent();
				// fmt::print(" - key {}\n", kv.first.toString());
				// fmt::print(" - par {}\n", parentKey.toString());
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
		std::vector<std::string> paths;
		std::vector<typename GtTypes::Coord> roots;

		std::unordered_map<typename GtTypes::Coord, typename GtTypes::BoundingBox, typename GtTypes::Coord::Hash> map;

	public:

		using Iterator = typename decltype(map)::const_iterator;
		inline Iterator begin() const { return map.begin(); }
		inline Iterator end()   const { return map.end();   }
		inline uint32_t size()  const { return map.size();  }

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
	std::vector<typename GtTypes::DecodedCpuTileData> datas;
	bool isOpen;
};

template <class GtTypes>
struct GtRenderContext {
	const RenderState& rs;
	GtPooledData<GtTypes>& pooledData;
	const CasterStuff& casterStuff;
	// PipelineStuff& pipelineStuff;
	// GraphicsPipeline& pipeline;

	// vk::CommandBuffer& cmd;
	// VkCommandBuffer& cmd;
	std::vector<uint32_t> drawIds;
	uint32_t drawCount = 0;

};

struct GtUpdateCameraData {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	RowMatrix4f mvp;
	Vector3f zplus;
	Vector3f eye;
	Vector2f wh;
	float two_tan_half_fov_y;
	RowMatrix83f frustumCorners;
};

template <class GtTypes>
struct GtUpdateContext {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	typename GtTypes::DataLoader& dataLoader;
	typename GtTypes::ObbMap& obbMap;
	GtPooledData<GtTypes>& pooledData;
	uint32_t& nReq; // held by GtRenderer: incremented for each pushAsk() call

	// Camera/Screen info
	// RowMatrix4f mvp;
	// Vector3f zplus;
	// Vector3f eye;
	// Vector2f wh;
	// float two_tan_half_fov_y;
	// RowMatrix83f frustumCorners;
	GtUpdateCameraData cameraData;

	float sseThresholdClose, sseThresholdOpen;

	GtAsk<GtTypes> currentOpenAsk;
	//int nAsked = 0;
};

/*
// the default bounding box
// Derived should implement:
//         - float computeSse(gtuc);
template <class GtTypes, class Derived>
struct GtBoundingBox {
	//float computeSse(const GtTypes::UpdateContext& gtuc);
};
*/
struct GtOrientedBoundingBox {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	inline GtOrientedBoundingBox() : ctr(Vector3f::Zero()), extents(Array3f::Zero()), q(Quaternionf::Identity()) {}
	inline GtOrientedBoundingBox(const Vector3f& ctr, const Array3f& ex, const Quaternionf& q) : ctr(ctr), extents(ex), q(q) {}

	float computeSse(const GtUpdateCameraData& gtuc, const float geoError) const;

	void getEightCorners(RowMatrix83f& out) const;

	public:
		Vector3f ctr;
		Array3f extents;
		Quaternionf q;
};

struct __attribute__((packed)) GtDebugPushConstants {
	float posColors[(4+4)*8]; // xyzw | rgba
};


// GtPooledData has an array of these
struct GtTileData {
	// ExImage tex;
	// ExImage texOld;
	// ExBuffer verts;
	// ExBuffer inds;
	// bool mustWriteImageDesc = false;

	// GLuint tex, texOld, verts, inds;
	GLuint tex, verts, inds;

	uint32_t residentInds=0, residentVerts=0;
};

// Holds all GPU buffers, list of free tile buffers, and descriptor stuff
template <class GtTypes>
struct GtPooledData {
	// typename GtTypes::GlobalBuffer gtgb;
	// FIXME: replace
	// ExBuffer globalBuffer;
	// DescriptorSet globalDescSet;

	std::vector<GtTileData> datas;
	// vk::raii::DescriptorSetLayout descSetLayout = {nullptr};
	// vk::raii::DescriptorSet descSet = {nullptr};
	// DescriptorSet tileDescSet;

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
	inline ~GtTile() {
		for (auto child : children) if (child) delete child;
	}

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

	void init();

	inline void join() {
		mtx.lock();
		doStop = true;
		mtx.unlock();
		cv.notify_one();
		if (thread.joinable()) {
			fmt::print(" - [GtDataLoader] joining...\n");
			thread.join();
			fmt::print(" - [GtDataLoader] ...joined\n");
		}
	}

	protected:
		void internalLoop();
		std::thread thread;
		typename GtTypes::Renderer& renderer;

		std::condition_variable cv;
		std::mutex mtx;
		std::deque<GtAsk<GtTypes>> asks;
		bool doStop = false;

		// ExUploader uploader;
		// Queue uploaderQueue;
};

// Derived must implement:
//		bool initPipelinesAndDescriptors()
template <class GtTypes, class Derived>
struct GtRenderer : public Castable {

	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		GtRenderer(const typename GtTypes::Config &cfg_);

		inline ~GtRenderer() {
			loader.join();
			delete obbMap;
			for (auto root : roots) delete root;
			// sampler.destroy(*device);
		}

	protected:
		/*
		Device *device { nullptr };

		// Very inefficient pushConsant based debug rendering of BoundingBoxes. Shouldn't be used other than dbg so doesn't matter.
		void initDebugPipeline(TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);
		void renderDbg(RenderState& rs, Command& cmd);
		DescriptorSet dbgDescSet;
		*/

		void renderDbg(RenderState& rs);


		// Disallow compute sse and changing tile tree. Useful for debugging.
		bool updateAllowed = true;

		Shader normalShader;
		Shader castableShader;
		Shader debugShader;


	public:

		inline bool toggleUpdateAllowed() {
			updateAllowed = !updateAllowed;
			return updateAllowed;
		}
		bool debugMode; // Note: GT_DEBUG (at top of this file) must also be compiled with true!

		inline void setDebugMode(bool m) { debugMode = m; }
		inline void flipDebugMode() { debugMode = !debugMode; }

		typename GtTypes::Config cfg;

		// void init(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);
		void init(const AppConfig& cfg);

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
		// Helper function that constructs GtUpdateContext<> and calls update()
		void defaultUpdate(Camera* cam);

		void render(RenderState& rs);

		/*
		GraphicsPipeline gfxPipeline;
		// GraphicsPipeline casterPipeline;
		GraphicsPipeline dbgPipeline;

		Sampler sampler;
		*/

		// The number asked, and the number answered
		uint32_t nReq = 0, nRes = 0;
		uint32_t asksInflight() const { return nReq - nRes; }

		inline uint32_t activeTiles() { return GT_NUM_TILES - gtpd.available.size(); }
};


}
