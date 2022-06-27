#include "utils/eigen.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <deque>
#include <unistd.h>
#include <fstream>
#include <stack>

#include "frastVk/core/app.h"
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


// These will be the new buffer types. 'ex' means exclusive, because thread safety must me managed by user.
struct ExBuffer {
	vk::raii::Buffer buffer {nullptr};
};
struct ExImage {
};

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
	PipelineStuff& pipelineStuff;

	vk::CommandBuffer& cmd;
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
		thread = std::thread(&GtDataLoader::internalLoop, this);
	}

	void pushAsk(GtAsk<GtTypes>& ask);

	inline void join() {
		mtx.lock();
		doStop = true;
		mtx.unlock();
		cv.notify_one();
		fmt::print(" - [GtDataLoader] joining...\n");
		thread.join();
		fmt::print(" - [GtDataLoader] ...joined\n");
	}

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
		}

	public:
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
// GtTile
////////////////////////////

template <class GtTypes, class Derived>
void GtTile<GtTypes,Derived>::unload(typename GtTypes::UpdateContext& gtuc) {
	loaded = false;
	if (meshIds.size()) {
		gtuc.pooledData.deposit(meshIds);
		meshIds.clear();
	}
}

template <class GtTypes, class Derived>
void GtTile<GtTypes,Derived>::render(typename GtTypes::RenderContext& gtrc) {
	if (inner()) {
		for (auto c : children) c->render(gtrc);
	}

	else if (leaf()) {
		assert(loaded);

		for (int mi=0; mi<meshIds.size(); mi++) {
			auto idx = meshIds[mi];
			auto &td = gtrc.pooledData.datas[idx];
			if (td.residentInds <= 0) continue;

			// TODO Uncomment these
			//gtrc.cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*td.verts.buffer}, {0u});
			//gtrc.cmd.bindIndexBuffer(*td.inds.buffer, 0u, vk::IndexType::eUint16);

			if (sizeof(typename GtTypes::PushConstants) > 0) {
				typename GtTypes::PushConstants pushc{(Derived*)this, idx};
				//gtrc.cmd.pushConstants(*gtrc.pipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const typename GtTypes::PushConstants>{1, &pushc});
			}

			//gtrc.cmd.drawIndexed(td.residentInds, 1, 0,0,0);
		}
		gtrc.drawCount += meshIds.size();
	}
}

template <class GtTypes, class Derived>
typename GtTile<GtTypes,Derived>::UpdateStatus GtTile<GtTypes,Derived>::update(typename GtTypes::UpdateContext& gtuc, bool sseIsCached) {

	// if (openingAsLeaf() or opening() or closing()) {
	if (openingAsLeaf() or closing()) {
		//fmt::print(" - update {} [SKIPPING]\n", toString());
		return UpdateStatus::eNone;
	}

	if (leaf() or invalid()) {
		if (not sseIsCached) lastSSE = bb.computeSse(gtuc)*geoError;
		//if (coord.len < 9) lastSSE = 3;
		//fmt::print(" - update {}\n", toString());
		// fmt::print(" - {} has sse {}\n", coord.toString(), lastSSE);

		if (!root() and lastSSE < gtuc.sseThresholdClose) {
			// Should not close while opening a subtree
			assert (gtuc.currentOpenAsk.ancestor == nullptr);
			return UpdateStatus::eClose;

		} else if (lastSSE > gtuc.sseThresholdOpen) {
			if (terminal()) return UpdateStatus::eNone;

			bool loadMe = true;

			// Begin collecting subtree
			if (gtuc.currentOpenAsk.ancestor == nullptr) {
				assert(gtuc.currentOpenAsk.tiles.size() == 0);
				gtuc.currentOpenAsk.ancestor = (Derived*)this;
			}

			//if (!terminal())
			{
				// Create all children, if any want to open,
				// transition self to inner and recursively call update() on all children.
				if (children.size() == 0) ((Derived*)this)->createChildren(gtuc);
				if (children.size() == 0) flags |= TERMINAL;
				if (terminal()) {
					if (gtuc.currentOpenAsk.ancestor == (Derived*)this)
						gtuc.currentOpenAsk.ancestor = nullptr;
					return UpdateStatus::eNone;
				}

				assert(children.size() > 0); // or else would be terminal

				bool anyChildrenWantOpen = false;
				for (auto &child : children) {
					child->lastSSE = child->bb.computeSse(gtuc)*geoError;
					//if (child->coord.len < 9) child->lastSSE = 3;
					if (child->lastSSE > gtuc.sseThresholdOpen) anyChildrenWantOpen = true;
				}

				//if (false and anyChildrenWantOpen) {
				if (anyChildrenWantOpen) {
					fmt::print(" - (t {}) some children want to open, shortcutting.\n", coord.toString());
					loadMe = false;
					// state = INNER;
					flags |= OPENING;
					for (auto &child : children) {
						// child->state = LEAF;
						// lastSSE was just compute and set on the child
						child->update(gtuc, true);
					}
				}
			}

			GtAsk<GtTypes> &ask = gtuc.currentOpenAsk;
			if (loadMe) {
				//fmt::print(" - (t {}) opening {} children (ask ancestor {}).\n", coord.toString(), children.size(), gtuc.currentOpenAsk.ancestor->coord.toString());
				//state = INNER;
				flags |= OPENING;
				for (auto &c : children) {
					c->flags |= OPENING_AS_LEAF;
					ask.tiles.push_back(c);
				}
			} else {
				// Even if some children are recursively loading and we dont 'loadMe', we still must sweep and add terminal children!
				for (auto &c : children) {
					if (c->terminal()) {
						c->flags |= OPENING_AS_LEAF;
						ask.tiles.push_back(c);
					}
				}
			}
			if (gtuc.currentOpenAsk.ancestor == (Derived*)this) {
				ask.isOpen = true;
				gtuc.dataLoader.pushAsk(ask);
				gtuc.currentOpenAsk.ancestor = nullptr;
			}


			// TODO queue open
			return UpdateStatus::eOpen;
		} else {
			return UpdateStatus::eNone;
		}

	} else {
		// inner
		// fmt::print(" - update {}\n", toString());
		bool allClose = true, allOpen = true;
		std::vector<UpdateStatus> stats(children.size());
		for (int i=0; i<children.size(); i++) {
			auto& c = children[i];
			auto stat = c->update(gtuc);
			if (stat != UpdateStatus::eOpen) allOpen = false;
			if (stat != UpdateStatus::eClose) allClose = false;
			stats[i] = stat;
		}

		// TODO: first I will only support closing one level at a time. The full-recursive case is trickier
		if (allClose) {

			if (not sseIsCached) lastSSE = bb.computeSse(gtuc)*geoError;
			//if (coord.len < 9) lastSSE = 3;
			if (lastSSE > gtuc.sseThresholdOpen) {
				fmt::print(" - (t {}) children want to close, but this tile would just re-open. Not doing anything.)\n", coord.toString());
				return UpdateStatus::eNone;
			}

			// Close child leaves, with this node becoming a leaf.
			GtAsk<GtTypes> newAsk;
			newAsk.isOpen = false;
			newAsk.ancestor = (Derived*)this;
			flags |= CLOSING_CHILDREN;

			for (int i=0; i<children.size(); i++) {
				assert(children[i]->leaf());
				assert(not children[i]->opening());
				assert(not children[i]->closing());
				children[i]->flags |= CLOSING;
			}
			newAsk.tiles.push_back((Derived*)this);
			fmt::print(" - Asking to close {} ({} children)\n", coord.toString(), children.size());
			gtuc.dataLoader.pushAsk(newAsk);

			// Must return none!
			return UpdateStatus::eNone;
		} else {
			// not all child leafs don't want to close, nothing to do!
			return UpdateStatus::eNone;
		}

		/*
		if (allClose) {
			if (not sseIsCached) lastSSE = bb.computeSse(gtuc);


			if (!root() and lastSSE < gtuc.sseThresholdClose) {
				for (int i=0; i<children.size(); i++) children[i]->flags |= CLOSING;
				return UpdateStatus::eClose;
			} else {
				// Close them, with this tile becoming a leaf.

				GtAsk<GtTypes> newAsk;
				newAsk.isOpen = false;
				newAsk.ancestor = (Derived*)this;
				flags |= CLOSING_CHILDREN;

				for (int i=0; i<children.size(); i++) {
						assert(not children[i]->opening());
						assert(not children[i]->closing());
						children[i]->flags |= CLOSING;
						newAsk.tiles.push_back(children[i]);
				}
				gtuc.dataLoader.pushAsk(newAsk);
			}

		} else {

			// Close only those that need closing
			for (int i=0; i<children.size(); i++) {
				if (stats[i] == UpdateStatus::eClose) {
					assert(not children[i]->opening());
					assert(not children[i]->closing());
					GtAsk<GtTypes> newAsk0;
					children[i]->flags |= CLOSING;
					newAsk0.ancestor = (Derived*)this;
					newAsk0.isOpen = false;
					newAsk0.tiles.push_back(children[i]);
					gtuc.dataLoader.pushAsk(newAsk0);
				}
			}
			flags |= CLOSING_CHILDREN;

		}
		*/
	}
	return UpdateStatus::eNone;
}

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
	// 1) Traverse tree, compute sse, update state, make any queries as needed
	// 2) Handle loaded results
	//
	// TODO:
	// Decide whether to have third thread that just computes sse : NO
	// Decide whether to have a map of OBBs seperate from tile data in files : YES
	//

	// 1)

	for (auto& root : roots) {
		// fmt::print(" (update root {})\n", root->coord.toString());
		root->update(gtuc);
	}

	fmt::print(" - PostUpdate\n");
	std::stack<typename GtTypes::Tile*> st;
	for (auto rt : roots) st.push(rt);
	while (not st.empty()) {
		auto t = st.top();
		st.pop();
		for (auto c : t->children) st.push(c);
		fmt::print(" - {}\n", t->toString());
	}

	// 2)

	decltype(waitingResults) theResults;
	{
		std::lock_guard<std::mutex> lck(askMtx);
		theResults = std::move(waitingResults);
		waitingResults.clear();
	}

	if (theResults.size()) {
		int ntot = 0;
		for (auto &r : theResults) ntot += r.tiles.size();
		fmt::print(fmt::fg(fmt::color::yellow), " - [#update] handling {} loaded results ({} total tiles)\n", theResults.size(), ntot);

		for (auto &r : theResults) {
			/*
			auto parent = r.ancestor;

			if (r.isOpen) {
				if (parent) assert(parent->opening());
				if (parent) assert(not parent->closing());
				for (auto tile : r.tiles) {
					assert(tile->openingAsLeaf());
					assert(not tile->opening());
					assert(not tile->closing());
					tile->flags &= ~GtTypes::Tile::OPENING_AS_LEAF;
					tile->loaded = true;
				}
				if (parent) {
					assert(parent->flags & GtTypes::Tile::OPENING);
					parent->flags &= ~GtTypes::Tile::OPENING;
					parent->unload(gtuc);
				}
			} else {

				assert(parent);
				assert(parent->closingChildren());
				assert(not parent->loaded);
				assert(parent->leaf());
				for (auto c : parent->children) {
					assert(c->closing());
					c->unload(gtuc);
					delete c;
				}
				parent->loaded = true;
				parent->state = GtTypes::Tile::LEAF;
			}
		*/

			if (r.isOpen) {
				for (auto tile : r.tiles) {
					//fmt::print(" - handle {}\n", tile->coord.toString());
					//if (r.ancestor) assert(r.ancestor->leaf() or r.ancestor->invalid());
					// assert(tile->invalid());
					assert(tile->openingAsLeaf());
					assert(not tile->opening());
					assert(not tile->closing());
					tile->state = GtTypes::Tile::LEAF;
					tile->flags &= ~GtTypes::Tile::OPENING_AS_LEAF;
					tile->loaded = true;
					
					for (auto up = tile->parent; up; up = up->parent) {
						//fmt::print(" - Setting no longer opening: {}\n", up->coord.toString());
						if (up->opening()) up->flags &= ~GtTypes::Tile::OPENING;
						if (!up->inner()) up->state = GtTypes::Tile::INNER;
						if (up->loaded) up->unload(gtuc);
						if (up == r.ancestor) break;
					}
				}
			}

			else {
				// Supports only one-level closing (see above)
				assert(r.ancestor);
				assert(r.ancestor->closingChildren());
				assert(r.ancestor->inner());
				assert(r.tiles.size() == 1);
				auto tile = r.tiles[0];
				assert(tile->children.size());
				for (auto child : tile->children) {
					assert(child->closing());
					assert(child->leaf());
					child->unload(gtuc);
					delete child;
				}
				tile->children.clear();
				tile->state = GtTypes::Tile::LEAF;
				tile->flags &= ~GtTypes::Tile::CLOSING_CHILDREN;
				tile->loaded = true;
			}
		}
	}

}

template <class GtTypes, class Derived>
void GtRenderer<GtTypes,Derived>::render(RenderState& rs, vk::CommandBuffer& cmd) {
	PipelineStuff* thePipelineStuff = nullptr;
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 1, {1,&*gtpd.descSet}, nullptr);
	//cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	thePipelineStuff = &pipelineStuff;

	typename GtTypes::RenderContext gtrc { rs, gtpd, *thePipelineStuff, cmd };

	for (auto root : roots) {
		root->render(gtrc);
	}

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
	mtx.lock();
	asks.push_back(std::move(ask));
	mtx.unlock();
	cv.notify_one();
	ask.ancestor = nullptr;
}

template <class GtTypes, class Derived>
void GtDataLoader<GtTypes,Derived>::internalLoop() {
	fmt::print(" - [GtDataLoader] starting loader thread\n");

	while (true) {
		std::unique_lock<std::mutex> lck(mtx);
		cv.wait(lck, [this]() { return doStop or asks.size(); });

		if (doStop) break;

		// Move the @asks member to a temporary buffer, and empty it.
		decltype(asks) curAsks;
		{
			curAsks = std::move(asks);
			asks.clear();
			lck.unlock();
		}

		fmt::print(fmt::fg(fmt::color::olive), " - [#loader] handling {} asks\n", curAsks.size());
		for (auto ask : curAsks)
			if (ask.ancestor)
				fmt::print(fmt::fg(fmt::color::olive), " - [#loader] ancestor {}\n", ask.ancestor->coord.toString());
			else
				fmt::print(fmt::fg(fmt::color::olive), " - [#loader] ancestor <null>\n");

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
					// auto error = static_cast<Derived*>(this)->uploadTile(tile, meshData, renderer.gtpd.datas[idx]);
					auto error = tile->upload(meshData, renderer.gtpd.datas[idx]);
					assert(not error);
				}

				fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {}\n", tile->coord.toString());
				tile->loaded = true;
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
float GtBoundingBox<GtTypes>::computeSse(const typename GtTypes::UpdateContext& gtuc) const {

	/*
	 gtuc:
			RowMatrix4f mvp;
			Vector3f zplus;
			Vector3f eye;
			Vector2f wh;
			float two_tan_half_fov_y;
	*/

	Vector3f ctr_ = (gtuc.mvp * ctr.homogeneous()).hnormalized();
	fmt::print(" - proj ctr {}\n", ctr_.transpose());
	Eigen::AlignedBox<float,3> cube { Vector3f{-1,-1,0}, Vector3f{1,1,1} };
	if (!cube.contains(ctr_)) {
		Eigen::Matrix<float,8,3,Eigen::RowMajor> corners;
		for (int i=0; i<8; i++)
			corners.row(i) << i%2, (i/2)%2, (i/4);
		RowMatrix3f R = q.toRotationMatrix();
		// corners = ((corners * 2 - 1).rowwise() * R.transpose() * extents).rowwise() + ctr;
		corners = ((corners.array() * 2 - 1).matrix() * R.transpose()).rowwise() + ctr.transpose();
		bool anyInside = false;
		for (int i=0; i<8; i++) if (cube.contains(corners.row(i).transpose())) { anyInside = true; break; }

		// Neither the center is in the box, nor any corner. Therefore the SSE is zero.
		if (!anyInside) return 0;
	}

	// Either center or any corner in box. Now evalauted oriented-box exterior distance to eye.
	// To get that distance I use a the function "sdBox" from IQ's raymarching function collection.
	// https://iquilezles.org/articles/distfunctions/

	RowMatrix3f R = q.toRotationMatrix();
	Vector3f q = R.transpose() * (gtuc.eye - ctr);
	float exteriorDistance = q.array().max(0.f).matrix().norm() + std::min(q.maxCoeff(), 0.f);

	float sse_without_geoErr = gtuc.wh(1) / (exteriorDistance * gtuc.two_tan_half_fov_y);
	fmt::print(" - (sseWoGeoErr {}) (extDist {})\n", sse_without_geoErr, exteriorDistance);
	return sse_without_geoErr; // Note: Still multiply by geoError of tile
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

	// Unfortunately, we need an adaptor to the generic constructor :(
	inline RtDataLoader(typename RtTypes::Renderer& renderer_) : GtDataLoader<RtTypes, RtDataLoader>(renderer_) {
	}

	inline int loadTile(RtTile* tile, RtTypes::DecodedCpuTileData& td) {
		return 1;
	}
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
		geoError = (1.f / R1) / static_cast<float>(1 << coord.level());
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

	inline bool upload(RtTypes::DecodedCpuTileData::MeshData& dctd, GtTileData& td) {
		return false;
	}
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
};





int main() {

	RtRenderer renderer;

	sleep(1);


	for (int i=0; i<100; i++) {
		fmt::print(" (update)\n");
		GtUpdateContext<RtTypes> gtuc { renderer.loader, *renderer.obbMap, renderer.gtpd };
		gtuc.sseThresholdClose = .9;
		gtuc.sseThresholdOpen = 1.5;
		gtuc.two_tan_half_fov_y = 1.;
		gtuc.wh.setConstant(512);
		gtuc.mvp.setIdentity();
		gtuc.zplus = decltype(gtuc.zplus)::UnitZ();
		renderer.update(gtuc);
		sleep(1);


		fmt::print(" (render)\n");
		RenderState rs;
		vk::raii::CommandBuffer cmd_ {nullptr};
		auto cmd = *cmd_;
		renderer.render(rs, cmd);
	}


	return 0;
}
