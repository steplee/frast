#include "rt.h"
#include <fmt/color.h>
#include <fmt/ostream.h>
#include <iostream>

#include <experimental/filesystem>

// #define dprint(...) fmt::print(__VA_ARGS__)
#define dprint(...)

#include "rt_decode.hpp"

// #include "shaders/compiled/all.hpp"
#include "frastVk/core/load_shader.hpp"


namespace {
	using namespace rt;
	bool projection_xsects_ndc_box(const Vector2f& a, const Vector2f& b) {
		//Vector2f tl { std::min(a(0), b(0)), std::min(a(1), b(1)) };
		//Vector2f br { std::max(a(0), b(0)), std::max(a(1), b(1)) };
		//return Eigen::AlignedBox2f { tl, br }.intersects(
		return Eigen::AlignedBox2f { a, b }.intersects(
					Eigen::AlignedBox2f { Vector2f{-1,-1}, Vector2f{1,1} });
	}

	using std::to_string;
	std::string to_string(const RtTile::State& s) {
		if (s == RtTile::State::LOADING) return "LOADING";
		if (s == RtTile::State::LOADING_INNER) return "LOADING_INNER";
		if (s == RtTile::State::LEAF) return "LEAF";
		if (s == RtTile::State::INNER) return "INNER";
		if (s == RtTile::State::OPENING) return "OPENING";
		if (s == RtTile::State::CLOSING) return "CLOSING";
		if (s == RtTile::State::NONE) return "none";
		return "???";
	}
}




namespace rt {



/* ===================================================
 *
 *
 *                   Miscellaneous
 *
 *
 * =================================================== */

PooledTileData::PooledTileData(RtCfg &cfg) : cfg(cfg) {
	available.resize(cfg.maxTiles);
	for (uint32_t i=0; i<cfg.maxTiles; i++) available[i] = i;
}


bool PooledTileData::withdraw(std::vector<uint32_t>& ids, bool isClose) {
	for (int i=0; i<ids.size(); i++) {
		if (
				(isClose and available.size() == 0)
				or (!isClose and available.size() < RtCfg::tilesDedicatedToClosing)) return true;
		ids[i] = available.back();
		available.pop_back();
	}
	return false;
}
bool PooledTileData::deposit(std::vector<uint32_t>& ids) {
	for (int i=0; i<ids.size(); i++) {
		available.push_back(ids[i]);
		if (available.size() >= cfg.maxTiles)
			throw std::runtime_error("Pushed id that set over initial maxTiles, should not happen");
	}
	return false;
}

/* ===================================================
 *
 *
 *                    RtTile
 *
 *
 * =================================================== */

RtTile::RtTile() {}
RtTile::~RtTile() {}

// TODO: This is unacceptably slow. I think a better way is possible. Really want to avoid testing all 8 points...
// WELL: If you pass the threshold, can check distances FIRST then check corners...
float RtTile::computeSSE(const RtUpdateContext& tuc) {
	Eigen::Matrix<float,8,4,Eigen::RowMajor> corners1;

	//corners1.topLeftCorner<2,3>() = corners;
	for (int i=0; i<8; i++) {
		int xi = (i  ) % 2;
		int yi = (i/2) % 2;
		int zi = (i/4) % 2;
		corners1.block<1,3>(i,0) << corners(xi,0), corners(yi,1), corners(zi,2);
	}

	corners1.rightCols<1>().setConstant(1.f);
	Eigen::Matrix<float,8,3,Eigen::RowMajor> corners2 = (corners1 * tuc.mvp.transpose()).rowwise().hnormalized();
	//if (not projection_xsects_ndc_box(corners2.block<1,2>(0,0).transpose(), corners2.block<1,2>(1,0).transpose()))

	Vector2f tl{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},
			 br{-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
	int n_invalid = 0;
	for (int i=0; i<8; i++) {
		// Note: Clip the far-plane at a relaxed 2.5 instead of 1, in-case the Camera class under-estimates the z_far value
		if (corners2(i,2) > 0 and corners2(i,2) < 2.5) {
			tl(0) = std::min(tl(0), corners2(i,0));
			tl(1) = std::min(tl(1), corners2(i,1));
			br(0) = std::max(br(0), corners2(i,0));
			br(1) = std::max(br(1), corners2(i,1));
		} else n_invalid++;
	}

	if (n_invalid == 8 or not projection_xsects_ndc_box(tl,br)) {
		// fmt::print(" - [#computeSSE] n_invalid is 8, returning 0 sse.\n");
		return 0;
	}

	float dist = (tuc.eye.transpose() - corners.colwise().mean()).norm();
	auto sse = geoError * tuc.wh(1) / (dist * tuc.two_tan_half_fov_y);
	// fmt::print(" - [#computeSSE] sse = {} (n_invalid {})\n", sse, n_invalid);
	return sse;
}

void RtTile::update(const RtUpdateContext& rtuc, RtTile* parent) {
	this->mask = 0;
	if (parent) parent->mask |= 1 << (nc.key[nc.level()-1] - '0');

	if (state == State::LOADING
		or state == State::LOADING_INNER
		or state == State::OPENING
		or state == State::CLOSING
		or state == State::NONE) {
		// If in transitory state, do nothing

	} else if (state == State::INNER) {

		for (auto c : children) c->update(rtuc, this);

		// If all children okay to close, queue load self and goto CLOSING
		// If parent is nullptr, this node is the virtual root. So never close it.
		if (children.size() > 0 and parent) {
			bool okay_to_close = true;
			for (auto c : children) if (c->state != State::LEAF or not c->want_to_close) okay_to_close = false;
			if (okay_to_close) {
				// Check to make sure we don't immediately want to re-open, which will repeat oscillating.
				lastSSE = computeSSE(rtuc);
				if (lastSSE < rtuc.sseThresholdOpen * .99) {
					// fmt::print(" - [#update] inner tile {} okay_to_close, closing children, loading self.\n", nc.key);
					state = State::LOADING_INNER;
					for (auto c : children) c->state = State::CLOSING;
					RtDataLoader::Ask ask;
					ask.isClose = true;
					ask.parent = this;
					ask.tiles = {this};
					rtuc.dataLoader.pushAsk(ask);
				}
			}
		}
	} else if (state == State::LEAF) {
		// Compute sse and mark want_to_close or open
		lastSSE = computeSSE(rtuc);
		if (lastSSE <= rtuc.sseThresholdClose) {
			// fmt::print(" - [#update] leaf tile {} want_to_close.\n", nc.key);
			want_to_close = true;
		} else
			want_to_close = false;
		if (lastSSE >= rtuc.sseThresholdOpen and childrenMissing == MissingStatus::NOT_MISSING) {
			state = State::OPENING;
			RtDataLoader::Ask ask;
			for (int8_t i=0; i<8; i++) {
				NodeCoordinate nnc(nc, '0'+i); // TODO Make this much more efficient. Store in each tile up-to 8 iterators pointing to node coordinate hash-set/tree.
				if (rtuc.dataLoader.tileExists(nnc)) {
					ask.tiles.push_back(new RtTile());
					ask.tiles.back()->nc = nnc;
				}
			}
			ask.isClose = false;
			ask.parent = this;
			// fmt::print(" - [#update] leaf tile {} opening {} children sse {}.\n", nc.key, ask.tiles.size(), lastSSE);
			rtuc.dataLoader.pushAsk(ask);
		} else if (lastSSE >= rtuc.sseThresholdOpen) {
			// fmt::print(" - [#update] leaf tile {}/{} sse {}, not opening zero children\n", nc.key, nc.level(), lastSSE);
		}
	}


}
void RtTile::render(RtRenderContext& rtc) {
	if ((state == State::INNER or state == State::LEAF or state == State::OPENING or state == State::CLOSING) and loaded and not noData) {

		if (state == State::INNER) for (auto c : children) c->render(rtc);

		if (loaded) {
			auto &cmd = rtc.cmd;
			int mi = 0;
			for (auto& mesh : meshes) {
				assert(mesh.idx != NO_INDEX);
				auto &td = rtc.pooledTileData.datas[mesh.idx];
				cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*td.verts.buffer}, {0u});
				cmd.bindIndexBuffer(*td.inds.buffer, 0u, vk::IndexType::eUint16);
				// if (meshes.size() > 1) fmt::print(" - [#RtTile::render] tile {}/{} mesh {} idx {} with {} inds.\n", nc.key, nc.level(), mi, mesh.idx, td.residentInds);

				RtPushConstants pushc;
				pushc.index = mesh.idx;
				pushc.octantMask = mask;
				pushc.level = nc.level();
				cmd.pushConstants(*rtc.pipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const RtPushConstants>{1, &pushc});

				cmd.drawIndexed(td.residentInds, 1, 0,0,0);
				mi++;
			}
		}
	} else if (children.size() != 0) {
		// fmt::print(" - node {} calling render on {} children\n", nc.key, children.size());
		for (auto c : children) c->render(rtc);
	}
}

bool RtTile::unload(PooledTileData& ptd, BaseVkApp* app) {
	std::vector<uint32_t> ids;
	for (auto& mesh : meshes) {
		assert(mesh.idx != NO_INDEX);

		ids.push_back(mesh.idx);
		mesh.idx = NO_INDEX;
	}
	return ptd.deposit(ids);
}
bool RtTile::unloadChildren(PooledTileData& ptd, BaseVkApp* app) {
	std::vector<uint32_t> ids;
	for (auto c : children) {
		assert(c);
		for (auto& mesh : c->meshes) {
			assert(mesh.idx != NO_INDEX);
			ids.push_back(mesh.idx);
			mesh.idx = NO_INDEX;
		}
		c->loaded = false;
	}
	return ptd.deposit(ids);
}

/* ===================================================
 *
 *
 *                    RtDataLoader
 *
 *
 * =================================================== */


bool RtDataLoader::tileExists(const NodeCoordinate& nc) {
	while (!populated) { usleep(100'000); }
	return existingNodes.find(nc) != existingNodes.end();
}
RtDataLoader::~RtDataLoader() {
	shutdown();
}
void RtDataLoader::shutdown() {
	shouldStop = true;
	if (internalThread.joinable()) internalThread.join();
}

bool RtDataLoader::loadRootTile(RtTile* rt_tile) {
	while (!populated) { usleep(100'000); }

	rt_tile->loaded = false;
	rt_tile->noData = true;
	rt_tile->state = RtTile::State::OPENING;

	rt_tile->childrenMissing = RtTile::MissingStatus::NOT_MISSING;
	rt_tile->nc = NodeCoordinate("");

	Ask ask;
	ask.parent = rt_tile;
	ask.isClose = false;
	for (auto nc : roots) {
		rt_tile->children.push_back(new RtTile());
		rt_tile->children.back()->nc = nc;
		rt_tile->children.back()->state = RtTile::State::LOADING;
		ask.tiles.push_back(rt_tile->children.back());
	}
	pushAsk(ask);

	/*
	for (auto nc : roots) {
		RtTile* tile = new RtTile();

		tile->nc = nc;
		tile->idx = pooledTileData.available.back();
		pooledTileData.available.pop_back();

		std::lock_guard<std::mutex> lck(mtx);

		loadTile(tile);

		void* rtgd_buf = (void*) pooledTileData.globalBuffer.mem.mapMemory(0, sizeof(RtGlobalData), {});
		std::vector<float>& mm = tile->modelMatf;
		mm[15] = 1;
		memcpy(((uint8_t*)rtgd_buf) + sizeof(float)*16*(1+tile->idx), mm.data(), sizeof(float)*16);
		memcpy(((uint8_t*)rtgd_buf) + sizeof(float)*16*(1+cfg.maxTiles) + sizeof(float)*(4*tile->idx), tile->uvScaleAndOffset.data(), sizeof(float)*4);
		pooledTileData.globalBuffer.mem.unmapMemory();

		tile->loaded = true;
		tile->state = RtTile::State::LEAF;

		rt_tile->children.push_back(tile);
	}
	*/


	return false;
}
bool RtDataLoader::pushAsk(const Ask& ask) {
	// Can't do here: we don't have n_meshes yet.
	/*
	int n = 0;
	for (auto tile : ask.tiles) n += tile->meshes.size();
	std::vector<uint32_t> ids(n, NO_INDEX);

	while (true) {
		if (pooledTileData.withdraw(ids)) {
			fmt::print(" - [#pushTile()] failed to withdraw {} (stack empty?)\n", ids.size());
		}
			fmt::print(" - [#pushTile()] failed to pushAsk with {} new (stack empty?)\n", ask.tiles.size());
		else break;
		usleep(20'000);
	}
	for (int i=0; i<ask.tiles.size(); i++) ask.tiles[i]->idx = ids[i];
	int i = 0;
	for (auto tile : ask.tiles)
		for (auto& mesh : tile->meshse) mesh.idx = ids[i++];
	// for (int i=0; i<ask.tiles.size(); i++) fmt::print( " - ask {} : {}\n", ask.tiles[i]->nc.key, ask.tiles[i]->idx);
	// fmt::print(" - withdrew {} {} {} {}\n",ids[0],ids[1],ids[2],ids[3]);
	*/

	asks.push_back(ask);

	return false;
}

// Check if all child are not available. Return true if and only if all are not. Can be used from any thread.
bool RtDataLoader::childrenAreMissing(const NodeCoordinate& nc) {
	int level = nc.level();
	bool have_any = false;
	for (int8_t i=0; i<8; i++) {
		NodeCoordinate nnc { nc, (char)(i+'0') };
		if (existingNodes.find(nnc) != existingNodes.end()) have_any = true;
	}
	return not have_any;
}

void RtDataLoader::init() {
	internalThread = std::thread(&RtDataLoader::internalLoop, this);
}


/*
 * loadTile is always called on the worker thread (except for root tiles).
 * After loadTile() returns vertex, index and texture buffers are ready to go.
 * The 'global' camera uniform buffer is not updated with the model matrix yet, though.
 * That is done in the render thread when the tile is swapped in.
 */
RtDataLoader::LoadStatus RtDataLoader::loadTile(RtTile* tile, bool isClose) {
	assert(existingNodes.find(tile->nc) != existingNodes.end());



	std::string path = cfg.rootDir + "/node/" + std::string(tile->nc.key);
	std::ifstream ifs(path);
	DecodedTileData dtd;
	fmt::print(" - Decoding {}\n",path);
	if (decode_node_to_tile(ifs, dtd)) {
		// throw std::runtime_error("Failed to load tile " + std::string(tile->nc.key));
		fmt::print(fmt::fg(fmt::color::orange), " - [#loadTile] decode failed, skipping tile.\n");
		tile->loaded = true;
		return LoadStatus::eFailed;
	}


	Eigen::Matrix<uint8_t,3,1> min = {255,255,255};
	Eigen::Matrix<uint8_t,3,1> max = {0,0,0};


	tile->meshes.resize(dtd.meshes.size());
	for (int i=0; i<dtd.meshes.size(); i++) {

		auto& mesh = tile->meshes[i];
		std::vector<uint32_t> idxs(1,NO_INDEX);
		if (pooledTileData.withdraw(idxs, isClose)) {
			fmt::print(" - [loatTile] Failed to withdraw an idx, skipping...\n");
			tile->meshes.clear();
			return LoadStatus::eTryAgain;
		}
		mesh.idx = idxs[0];
		// assert(mesh.idx != NO_INDEX);

		auto& td = pooledTileData.datas[mesh.idx];
		auto& md = dtd.meshes[i];

		if (md.texSize[0] > 0) {
			if(0) printf(" - upload to idx %u, tex size %u %u %u resident tex size %u %u %u\n",
					mesh.idx,
					md.texSize[0],
					md.texSize[1],
					md.texSize[2],
					td.tex.extent.height,
					td.tex.extent.width,
					td.tex.extent.depth
					);
			// md.texSize[0] = 256;
			// md.texSize[1] = 256;

			if (md.texSize[0] == td.tex.extent.height and md.texSize[1] == td.tex.extent.width) {
				myUploader.uploadSync(td.tex, md.img_buffer_cpu.data(), md.texSize[0]*md.texSize[1]*md.texSize[2], 0);
			} else {
				td.texOld = std::move(td.tex);
				td.tex = ResidentImage{};
				// td.tex.createAsTexture(myUploader, md.texSize[0], md.texSize[1], cfg.textureFormat, md.img_buffer_cpu.data());
				td.tex.createAsTexture(myUploader, md.texSize[0], md.texSize[1], cfg.textureFormat, nullptr);
				myUploader.uploadSync(td.tex, md.img_buffer_cpu.data(), md.texSize[0]*md.texSize[1]*md.texSize[2], 0);
				td.mustWriteImageDesc = true;
			}
		}

		auto v_size = sizeof(PackedVertex)*md.vert_buffer_cpu.size();
		auto i_size = sizeof(uint16_t)*md.ind_buffer_cpu.size();
		if (v_size > td.verts.residentSize) {
			td.verts.setAsVertexBuffer(v_size, false);
			td.verts.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		}
		if (i_size > td.inds.residentSize) {
			td.inds.setAsIndexBuffer(i_size, false);
			td.inds.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		}
		myUploader.uploadSync(td.verts, md.vert_buffer_cpu.data(), v_size, 0);
		myUploader.uploadSync(td.inds, md.ind_buffer_cpu.data(), i_size, 0);
		td.residentInds = md.ind_buffer_cpu.size();

		tile->modelMatf.resize(16);


		// tile->uvScaleAndOffset = {md.uvScale[0], md.uvScale[1], md.uvOffset[0], md.uvOffset[1]};
		mesh.uvScaleAndOffset.resize(4);
		mesh.uvScaleAndOffset[0] = md.uvScale[0];
		mesh.uvScaleAndOffset[1] = md.uvScale[1];
		mesh.uvScaleAndOffset[2] = md.uvOffset[0];
		mesh.uvScaleAndOffset[3] = md.uvOffset[1];

		Eigen::Map<Eigen::Matrix<uint8_t,-1,sizeof(PackedVertex),Eigen::RowMajor>> verts { (uint8_t*) md.vert_buffer_cpu.data() , (long)md.vert_buffer_cpu.size() , sizeof(PackedVertex)};
		Eigen::Matrix<uint8_t,3,1> min_ = verts.leftCols(3).colwise().minCoeff().transpose();
		Eigen::Matrix<uint8_t,3,1> max_ = verts.leftCols(3).colwise().maxCoeff().transpose();
		for (int i=0; i<3; i++) min(i) = std::min(min_(i), min(i));
		for (int i=0; i<3; i++) max(i) = std::max(max_(i), max(i));
	}

	Eigen::Map<Eigen::Matrix4d> mm(dtd.modelMat);
	constexpr double R1         = (6378137.0);
	constexpr double R1i = 1.0 / R1;
	Eigen::Matrix4d scale_m; scale_m <<
		R1i, 0, 0, 0,
		0, R1i, 0, 0,
		0, 0, R1i, 0,
		0, 0, 0, 1;
	mm = scale_m * mm;

	// fmt::print(" - transformed MM:\n{}\n", mm);
	// std::cout << " - Model low: " << (mm * Eigen::Vector4d{0,0,0,1}).transpose() << "\n";
	// std::cout << " - Model hi : " << (mm * Eigen::Vector4d{255,255,255,1}).transpose() << "\n";
	// std::cout << " - Model low r: " << (mm * Eigen::Vector4d{160,0,0,1}).norm() << "\n";
	// std::cout << " - Model hi  r: " << (mm * Eigen::Vector4d{160,255,255,1}).norm() << "\n";
	// NOTE: For now, setting to this just for testing TODO XXX
	// for (int i=0; i<16; i++) mm[i] = ((i % 5) == 0) / 255.f;

	for (int i=0; i<16; i++) tile->modelMatf[i] = (float) dtd.modelMat[i];

	if (dtd.metersPerTexel <= 0) {
		// TODO: Is this a good approx?
		// dtd.metersPerTexel = 2 * R1 / (1 << tile->nc.level());
		// tile->geoError = 4.0 * (1/255.) / (1 << tile->nc.level());
		tile->geoError = 8.0 * (1/255.) / (1 << tile->nc.level());
		// tile->geoError = 16.0 * (1/255.) / (1 << tile->nc.level());
	} else {
		// tile->geoError = dtd.metersPerTexel / (255. * R1);
		tile->geoError = dtd.metersPerTexel / (R1);
	}

	// Compute corners
	// Would be better to get the OBB from the bulk metadata, but this should suffice.
	Eigen::Matrix<float,2,3> corners_;
	corners_.row(0) = (mm.topLeftCorner<3,3>() * min.cast<double>() + mm.topRightCorner<3,1>()).cast<float>();
	corners_.row(1) = (mm.topLeftCorner<3,3>() * max.cast<double>() + mm.topRightCorner<3,1>()).cast<float>();
	tile->corners.row(0) = corners_.colwise().minCoeff();
	tile->corners.row(1) = corners_.colwise().maxCoeff();

	if (tile->childrenMissing == RtTile::MissingStatus::UNKNOWN) {
		bool have_any_children = false;
		for (int8_t i=0; i<8; i++) {
			NodeCoordinate nnc(tile->nc, '0'+i);
			have_any_children |= tileExists(nnc);
		}
		tile->childrenMissing = have_any_children ? RtTile::MissingStatus::NOT_MISSING : RtTile::MissingStatus::MISSING;
	}


	// fmt::print(" - [#RtDataLoader::loadTile] successfully loaded {}, lvl {}, geoError {}\n", tile->nc.key, tile->nc.level(), tile->geoError);
	// fmt::print(" -                          tl {}, r {}\n", tile->corners.row(0), tile->corners.norm());
	// fmt::print(" -                          br {}, r {}\n", tile->corners.row(1), tile->corners.norm());
	tile->loaded = true;

	return LoadStatus::eSuccess;
}

bool RtDataLoader::populateFromFiles() {
	namespace fs = std::experimental::filesystem;

	fmt::print(" - populating tiles, may take a while if not cached...\n");
	fs::path root { cfg.rootDir };
	auto bulkDir = cfg.rootDir / fs::path{"bulk"};
	auto nodeDir = cfg.rootDir / fs::path{"node"};
	auto nodeListFile = cfg.rootDir / fs::path{"fullNodeList"};
	int minimum_level = 99;
	int maximum_level = 0;
	// I don't actually use the bulk metadatas.
	/*
	for (const auto& entry : fs::directory_iterator{bulkDir}) {
		existingBulks.insert(NodeCoordinate{entry.path().stem()});
	}
	*/

	// If the python script is new and computes the nodeListFile use it.
	// Otherwise we have to iterate over directory contents (which is slow)
	if (fs::exists(nodeListFile)) {
		fmt::print(fmt::fg(fmt::color::steel_blue), " - [RtDataLoader] nodeListFile '{}' exists, using it.\n", nodeListFile);
		std::ifstream is {nodeListFile};
		while (is.good()) {
			std::string key;
			std::getline(is, key);
			if (key.length() == 0) break; // NOTE: No level 0 root allowed, and no empty lines allowed!!!
			existingNodes.insert(NodeCoordinate{key});
			minimum_level = std::min(minimum_level,(int)key.length());
			maximum_level = std::max(maximum_level,(int)key.length());
		}
	} else {
		fmt::print(fmt::fg(fmt::color::steel_blue), " - [RtDataLoader] nodeListFile '{}' DOES NOT exist, looping slowly over entire node dir...\n", nodeListFile);
		for (const auto& entry : fs::directory_iterator{nodeDir}) {
			std::string key = entry.path().stem();
			if (key == "_") key = "";
			existingNodes.insert(NodeCoordinate{key});
			minimum_level = std::min(minimum_level,(int)key.length());
			maximum_level = std::max(maximum_level,(int)key.length());
		}
	}

	for (auto n : existingNodes) {
		if (n.level() == minimum_level) roots.push_back(n);
	}
	fmt::print(" - {} nodes, minimum level {}, max {}, with {} root tiles.\n", existingNodes.size(), minimum_level, maximum_level, roots.size());

	populated = true;
	return false;
}

void RtDataLoader::internalLoop() {
	// Create uploader
	myUploadQueue = app->deviceGpu.getQueue(app->queueFamilyGfxIdxs[0], 1);
	myUploader = std::move(Uploader(app, *myUploadQueue));

	populateFromFiles();

	while (not shouldStop) {
		std::vector<Ask> curAsks;
		std::vector<Ask> nxtAsks;
		{
			std::lock_guard<std::mutex> lck(mtx);
			//std::swap(curAsks, this->asks);
			curAsks = std::move(this->asks);
		}
		if (curAsks.size())
			fmt::print(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] handling {} asks\n", curAsks.size());

		for (; curAsks.size(); curAsks.pop_back()) {
			Ask& ask = curAsks.back();
			// for (int i=0; i<ask.tiles.size(); i++) fmt::print( " - handle ask {}\n", ask.tiles[i]->nc.key);
			Result res;
			// Load either one parent or N children.
			if (ask.isClose == 1) {
				assert(ask.tiles.size() == 1);
				const NodeCoordinate &nc = ask.tiles[0]->nc;
				dprint(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] loading one ({})\n", nc.key);

				auto stat = loadTile(ask.tiles[0], true);
				if (stat == LoadStatus::eFailed) {
					fmt::print(fmt::fg(fmt::color::pink), " - [#Loader::internalLoop] not allowed to faile loading parent (should be impossible) ({})\n", nc.key);
					continue;
				} else if (stat == LoadStatus::eTryAgain) {
					fmt::print(fmt::fg(fmt::color::pink), " - [#Loader::internalLoop] not allowed to fail loading parent with eTryAgain (tree is dead-locked) ({})\n", nc.key);
					continue;
				} else if (stat == LoadStatus::eSuccess) {
					res.isClose = true;
					// res.tiles = std::move(ask.tiles);
					res.tiles = (ask.tiles);
					res.parent = ask.parent;
				}
			}
			else {
				assert(ask.parent && "a quad Ask must have a parent");
				NodeCoordinate parent_nc = ask.parent->nc;
				dprint(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] loading {} children (parent {})\n", ask.tiles.size(), parent_nc.key);
				bool failed = false;
				for (int i=0; i<ask.tiles.size(); i++) {

					// TODO: Withdraw idx's up front, that we we avoid loading some and immediately throwing them away!!!
					auto stat = loadTile(ask.tiles[i], false);
					if (stat == LoadStatus::eFailed) {
						const NodeCoordinate nc = ask.tiles[i]->nc;
						fmt::print(fmt::fg(fmt::color::pink), " - [#Loader::internalLoop] FAILED loading quad (ci {}/{}) ({})\n", i,8, nc.key);
						// failed = true;
					} else if (stat == LoadStatus::eTryAgain) {
						const NodeCoordinate nc = ask.tiles[i]->nc;
						fmt::print(fmt::fg(fmt::color::pink), " - [#Loader::internalLoop] failed open with eTryAgain, will try later ({})\n", nc.key);
						nxtAsks.push_back(std::move(ask));
						failed = true;
					} else if (stat == LoadStatus::eSuccess) {
					}
				}

				if (not failed) {
					res.isClose = false;
					// res.tiles = std::move(ask.tiles);
					res.tiles = (ask.tiles);
					res.parent = ask.parent;
				} else continue;
			}

			// Lock; push
			{
				std::lock_guard<std::mutex> lck(mtx);
				loadedResults.push_back(res);
			}
		}

		// Copy eTryAgain asks to next period
		{
			std::lock_guard<std::mutex> lck(mtx);
			for (auto& ask : nxtAsks) asks.push_back(std::move(ask));
		}

		usleep(63'000);
	}
}

/* ===================================================
 *
 *
 *                  RtRenderer
 *
 *
 * =================================================== */

RtRenderer::~RtRenderer() {
	// Destroy this first
	dataLoader.shutdown();

	cmdBuffers.clear();
	cmdPool = {nullptr};
}


void RtRenderer::stepAndRender(RenderState& rs, vk::CommandBuffer& cmd) {
	update(rs);
	render(rs, cmd);
}

void RtRenderer::update(RenderState& rs) {
	RtUpdateContext rtuc { dataLoader };
	rtuc.mvp = Eigen::Map<const RowMatrix4d> { rs.mvp() }.cast<float>();
	Vector3d eyed;
	rs.eyed(eyed.data());
	rtuc.eye = eyed.cast<float>();
	rtuc.wh = Vector2f { rs.camera->spec().w, rs.camera->spec().h };
	// rtuc.two_tan_half_fov_y = 2.f * std::tan(rs.camera->spec().vfov() * .5f);
	rtuc.two_tan_half_fov_y = rs.camera->spec().w / rs.camera->spec().fx();
	rtuc.sseThresholdOpen = 1.7;
	rtuc.sseThresholdClose = .8;

	// Update all tiles recursively. Will computeSSE, queue opens/closes
	if (root) {
		root->update(rtuc, nullptr);
	}

	auto writeImgDesc = [&](TileData& td, uint32_t idx) {
		std::vector<vk::DescriptorImageInfo> i_infos;
		i_infos.push_back(vk::DescriptorImageInfo{
				*td.tex.sampler,
				*td.tex.view,
				vk::ImageLayout::eShaderReadOnlyOptimal
				});
		std::vector<vk::WriteDescriptorSet> writeDesc = { {
			*pooledTileData.descSet,
				0, idx, (uint32_t)i_infos.size(),
				vk::DescriptorType::eCombinedImageSampler,
				i_infos.data(), nullptr, nullptr
		} };
		app->deviceGpu.updateDescriptorSets({(uint32_t)writeDesc.size(), writeDesc.data()}, nullptr);
		td.texOld = ResidentImage{}; // Free
	};
	auto checkWriteImgDesc = [&](RtTile* tile) {
		for (auto& mesh : tile->meshes) {
			if (pooledTileData.datas[mesh.idx].mustWriteImageDesc) {
				pooledTileData.datas[mesh.idx].mustWriteImageDesc = false;
				writeImgDesc(pooledTileData.datas[mesh.idx], mesh.idx);
			}
		}
	};


	// Handle loaded opens/closes
	{
		std::lock_guard<std::mutex> lck(dataLoader.mtx);

		void* rtgd_buf = (void*) pooledTileData.globalBuffer.mem.mapMemory(0, sizeof(RtGlobalData), {});

		if (dataLoader.loadedResults.size())
			fmt::print(fmt::fg(fmt::color::yellow), " - [#update] handling {} loaded results\n", dataLoader.loadedResults.size());
		for (; dataLoader.loadedResults.size(); dataLoader.loadedResults.pop_back()) {
			auto &res = dataLoader.loadedResults.back();
			RtTile* parent = res.parent;

			// Handle opening/closing tiles
			// TODO

			if (res.isClose) {
				// We've loaded a single parent to replace multiple children.
				// (Delete children) and (set self as leaf)

				assert(res.tiles.size() == 1);
				auto tile = res.tiles[0];
				// fmt::print(" - [#update] handling close at {}\n", tile->nc.key);

				tile->unloadChildren(pooledTileData, app);
				for (auto c : tile->children) {
					delete c;
				}
				tile->children.clear();

				tile->state = RtTile::State::LEAF;
				checkWriteImgDesc(tile);

			} else {
				// We've loaded N children to replace a single parent.
				// (unload parent) and (set parent as inner) and (set children as leaves)
				parent->state = RtTile::State::INNER;

				if (1) {
					if (parent->loaded) parent->unload(pooledTileData, app);
					parent->loaded = false;
				}

				// fmt::print(" - [#update] handling open at {} with {} children\n", parent->nc.key, res.tiles.size());
				for (int i=0; i<res.tiles.size(); i++) {
					auto tile = res.tiles[i];
					tile->state = RtTile::State::LEAF;
					checkWriteImgDesc(tile);
				}
				parent->children = (res.tiles);
			}

			// Handle model mats
			for (int i=0; i<res.tiles.size(); i++) {
				if (res.tiles[i]) {
					auto tile = res.tiles[i];
					std::vector<float>& mm = tile->modelMatf;
					for (auto& mesh : tile->meshes) {
						// The +4*4 is for the new 'offset' field
						memcpy(((uint8_t*)rtgd_buf) + 4*4 + sizeof(float)*(16)*(1+mesh.idx), mm.data(), sizeof(float)*16);
						memcpy(((uint8_t*)rtgd_buf) + 4*4 + sizeof(float)*(16)*(1+cfg.maxTiles) + sizeof(float)*(4*mesh.idx), mesh.uvScaleAndOffset.data(), sizeof(float)*4);
						mesh.uvScaleAndOffset.clear();
					}
					tile->modelMatf.clear();
				}
			}
		}
		pooledTileData.globalBuffer.mem.unmapMemory();

	}
}

void RtRenderer::render(RenderState& rs, vk::CommandBuffer& cmd) {

	PipelineStuff* thePipelineStuff = nullptr;

	if (casterStuff.casterMask != 0) {
		thePipelineStuff = &casterStuff.casterPipelineStuff;
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 1, {1,&*pooledTileData.descSet}, nullptr);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 2, {1,&*casterStuff.casterDescSet}, nullptr);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipeline);
	} else {
		thePipelineStuff = &pipelineStuff;
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 1, {1,&*pooledTileData.descSet}, nullptr);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	}

	RtRenderContext rtc {
			rs,
			pooledTileData,
			*thePipelineStuff,
			{},
			cmd
	};

	// Load global data (camera and such)
	RtGlobalData rtgd;
	
	// We need to fix float32 jumpyness by shifting the camera center.
	// I think this works because it avoids mixing position into rotation (todo: invesgiatge this)
	// There are three options:
	//		1) Do nothing (bad)
	//		2) Shift by nearest position on ellipsoid
	//		3) Shift by camera center, to zero it out.
	// Not sure if (2) or (3) is better, but (3) is simpler to implement and avoids an extra float64 matmul,
	// so I do it here.
	Vector3d offset;
	rs.eyed(offset.data());
	// offset = offset.normalized();
	// RowMatrix4d offsetMatrix = RowMatrix4d::Identity();
	// offsetMatrix.topRightCorner<3,1>() = offset;

	/*
	rs.mstack.reset();
	rs.mstack.push(rs.camera->proj());
	// rs.mstack.push(offsetMatrix.data());
	// rs.mstack.push(rs.camera->view());
	double mm[16];
	memcpy(mm,rs.camera->view(), 16*8);
	mm[0*4+3] = mm[1*4+3] = mm[2*4+3] = 0;
	rs.mstack.push(mm);

	rs.mvpf(rtgd.mvp);
	*/
	RowMatrix4d shift_(RowMatrix4d::Identity()); shift_.topRightCorner<3,1>() = offset;
	// Eigen::Map<const RowMatrix4d> proj_ { rs.camera->proj() };
	// Eigen::Map<const RowMatrix4d> view_ { rs.camera->view() };
	// RowMatrix4f new_mvp = (proj_ * view_ * shift_).cast<float>();
	Eigen::Map<const RowMatrix4d> mvp_ { rs.mstack.peek() };
	RowMatrix4f new_mvp = (mvp_ * shift_).cast<float>();
	memcpy(rtgd.mvp, new_mvp.data(), 4*16);

	rtgd.offset[0] = -offset(0);
	rtgd.offset[1] = -offset(1);
	rtgd.offset[2] = -offset(2);
	rtgd.offset[3] = 0;
	//fmt::print(" - [#render] copying {} bytes to global buffer.\n", size);
	// for (int i=0; i<rtc.drawTileIds.size(); i++) rtgd.drawTileIds[i] = rtc.drawTileIds[i];
	void* dbuf = (void*) pooledTileData.globalBuffer.mem.mapMemory(0, sizeof(RtGlobalData), {});
	memcpy(dbuf, &rtgd, sizeof(RtGlobalData::mvp) + sizeof(RtGlobalData::offset));
	pooledTileData.globalBuffer.mem.unmapMemory();

	if (root) root->render(rtc);
}

void RtRenderer::init() {
	dataLoader.init();

	// Global data (camera etc). It is host visible for convenient writing
	{
		pooledTileData.globalBuffer.setAsUniformBuffer(sizeof(RtGlobalData), true);
		pooledTileData.globalBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);
	}


	// Create resources and descriptors for tile data
	{
		pooledTileData.datas.resize(cfg.maxTiles);
		uint8_t *emptyImage = (uint8_t*)malloc(cfg.maxTextureEdge*cfg.maxTextureEdge*4);
		memset(emptyImage, 100, cfg.maxTextureEdge*cfg.maxTextureEdge*4);
		for (int i=0; i<cfg.maxTiles; i++) {
			auto &td = pooledTileData.datas[i];
			td.tex.createAsTexture(app->uploader, cfg.maxTextureEdge, cfg.maxTextureEdge, cfg.textureFormat, emptyImage);
			td.verts.setAsVertexBuffer(sizeof(PackedVertex)*cfg.maxVerts, false);
			td.inds.setAsIndexBuffer(sizeof(uint16_t)*cfg.maxInds, false);
			// td.ubo.setAsUniformBuffer(sizeof(RtNodeData), false);
			td.verts.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
			td.inds.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
			// td.ubo.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		}
		free(emptyImage);

		std::vector<vk::DescriptorPoolSize> poolSizes = {
			// vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
			// vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, 1 },
			// vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, cfg.maxTiles },
			vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 1 },
		};
		vk::DescriptorPoolCreateInfo poolInfo {
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
				cfg.maxTiles,
				(uint32_t)poolSizes.size(), poolSizes.data()
		};
		descPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

		// Bind textures and ubos. There are only two bindings, but each is an array of length @cfg.maxTiles
		{
			std::vector<vk::DescriptorSetLayoutBinding> bindings;
			// Texture array binding
			bindings.push_back({
					0, vk::DescriptorType::eCombinedImageSampler,
					cfg.maxTiles, vk::ShaderStageFlagBits::eFragment });
			// UBO
			bindings.push_back({
					1, vk::DescriptorType::eUniformBuffer,
					// cfg.maxTiles,
					1,
					vk::ShaderStageFlagBits::eVertex });

			vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
			pooledTileData.descSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

			vk::DescriptorSetAllocateInfo allocInfo {
				*descPool, 1, &*pooledTileData.descSetLayout
			};
			pooledTileData.descSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

			// descSet is allocated, now make the arrays point correctly on the gpu side.
			std::vector<vk::DescriptorBufferInfo> b_infos;
			std::vector<vk::DescriptorImageInfo> i_infos;

			b_infos.push_back(vk::DescriptorBufferInfo{
					// *pooledTileData.datas[j].ubo.buffer,
					// *pooledTileData.datas[0].ubo.buffer,
					*pooledTileData.globalBuffer.buffer,
					0,
					VK_WHOLE_SIZE
					});
			for (int j=0; j<cfg.maxTiles; j++) {
				i_infos.push_back(vk::DescriptorImageInfo{
						*pooledTileData.datas[j].tex.sampler,
						*pooledTileData.datas[j].tex.view,
						vk::ImageLayout::eShaderReadOnlyOptimal
				});
			}


			std::vector<vk::WriteDescriptorSet> writeDesc = {
				{
					*pooledTileData.descSet,
					0, 0, (uint32_t)i_infos.size(),
					vk::DescriptorType::eCombinedImageSampler,
					i_infos.data(),
					nullptr,
					nullptr
				}
				,{
					*pooledTileData.descSet,
					1, 0, (uint32_t)b_infos.size(),
					vk::DescriptorType::eUniformBuffer,
					// vk::DescriptorType::eStorageBuffer,
					nullptr,
					b_infos.data(),
					nullptr
				}
			};
			app->deviceGpu.updateDescriptorSets({(uint32_t)writeDesc.size(), writeDesc.data()}, nullptr);
		}
	}

	{
		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex } };

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, 1, bindings };
		globalDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *descPool, 1, &*globalDescSetLayout };
		globalDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		vk::DescriptorBufferInfo binfo {
			*pooledTileData.globalBuffer.buffer, 0, VK_WHOLE_SIZE
		};
		vk::WriteDescriptorSet writeDesc[1] = { {
			*globalDescSet,
				0, 0, 1,
				vk::DescriptorType::eUniformBuffer,
				nullptr, &binfo, nullptr } };
		app->deviceGpu.updateDescriptorSets({1, writeDesc}, nullptr);
	}

	// Create pipeline
	{
		PipelineBuilder plBuilder;

		loadShader(app->deviceGpu, pipelineStuff.vs, pipelineStuff.fs, "rt/rt1");

		pipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		MeshDescription md;
		md.posDims = 4;
		md.rows = cfg.maxVerts;
		// md.cols = 3 + 2;
		md.haveUvs = true;
		md.haveNormals = true;
		md.rowSize = 4*1 + 2*2 + 3*1 + 1;
		md.indType = vk::IndexType::eUint16;
		md.posType = MeshDescription::ScalarType::UInt8_scaled;
		md.uvType = MeshDescription::ScalarType::UInt16_scaled;
		md.normalType = MeshDescription::ScalarType::UInt8_scaled;
		VertexInputDescription vertexInputDescription = md.getVertexDescription();
		plBuilder.init(
				vertexInputDescription,
				// vk::PrimitiveTopology::eTriangleList,
				vk::PrimitiveTopology::eTriangleStrip,
				*pipelineStuff.vs, *pipelineStuff.fs);

		// Add Push Constants & Set Layouts.
		pipelineStuff.setLayouts.push_back(*globalDescSetLayout);
		pipelineStuff.setLayouts.push_back(*pooledTileData.descSetLayout);


		pipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eVertex,
				0,
				sizeof(RtPushConstants) });

		pipelineStuff.build(plBuilder, app->deviceGpu, *app->simpleRenderPass.pass, app->mainSubpass());
	}

	init_caster_stuff();

	// Find root frast tile, then create the Tile backing it.
	// Done on the current thread, so we synch load it
	{
		root = new RtTile();
		dataLoader.loadRootTile(root);
	}
}

void RtRenderer::init_caster_stuff() {
	//
	// Caster stuff (1: descriptor set and layout, 2: pipeline stuff)
	//

	do_init_caster_stuff(app);

	// Create pipeline stuff for caster
	{
		PipelineBuilder plBuilder;
		loadShader(app->deviceGpu, casterStuff.casterPipelineStuff.vs, casterStuff.casterPipelineStuff.fs, "rt/cast");

		casterStuff.casterPipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		MeshDescription md;
		md.posDims = 4;
		md.rows = cfg.maxVerts;
		// md.cols = 3 + 2;
		md.haveUvs = true;
		md.haveNormals = true;
		md.rowSize = 4*1 + 2*2 + 3*1 + 1;
		md.indType = vk::IndexType::eUint16;
		md.posType = MeshDescription::ScalarType::UInt8_scaled;
		md.uvType = MeshDescription::ScalarType::UInt16_scaled;
		md.normalType = MeshDescription::ScalarType::UInt8_scaled;
		VertexInputDescription vertexInputDescription = md.getVertexDescription();
		plBuilder.init(
				vertexInputDescription,
				vk::PrimitiveTopology::eTriangleStrip,
				*casterStuff.casterPipelineStuff.vs, *casterStuff.casterPipelineStuff.fs);

		// Add Push Constants & Set Layouts.
		casterStuff.casterPipelineStuff.setLayouts.push_back(*globalDescSetLayout);
		casterStuff.casterPipelineStuff.setLayouts.push_back(*pooledTileData.descSetLayout);

		// For the caster matrices and metadata, we have a third set.
		casterStuff.casterPipelineStuff.setLayouts.push_back(*casterStuff.casterDescSetLayout);

		casterStuff.casterPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eVertex,
				0,
				sizeof(RtPushConstants) });

		casterStuff.casterPipelineStuff.build(plBuilder, app->deviceGpu, *app->simpleRenderPass.pass, app->mainSubpass());
	}
}


}
