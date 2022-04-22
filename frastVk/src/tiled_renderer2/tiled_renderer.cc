#include "tiled_renderer.h"

#include <iostream>
#include <fmt/printf.h>
#include <fmt/ostream.h>
#include <fmt/color.h>

#include "conversions.hpp"

#include "shaders/compiled/all.hpp"


#if 0
#define dprint(...) fmt::print(__VA_ARGS__)
#else
#define dprint(...)
#endif


namespace {
	using namespace tr2;

	bool projection_xsects_ndc_box(const Vector2f& a, const Vector2f& b) {
		//Vector2f tl { std::min(a(0), b(0)), std::min(a(1), b(1)) };
		//Vector2f br { std::max(a(0), b(0)), std::max(a(1), b(1)) };
		//return Eigen::AlignedBox2f { tl, br }.intersects(
		return Eigen::AlignedBox2f { a, b }.intersects(
					Eigen::AlignedBox2f { Vector2f{-1,-1}, Vector2f{1,1} });
	}

	using std::to_string;
	std::string to_string(const Tile::State& s) {
		if (s == Tile::State::LOADING) return "LOADING";
		if (s == Tile::State::LOADING_INNER) return "LOADING_INNER";
		if (s == Tile::State::LEAF) return "LEAF";
		if (s == Tile::State::INNER) return "INNER";
		if (s == Tile::State::OPENING) return "OPENING";
		if (s == Tile::State::CLOSING) return "CLOSING";
		if (s == Tile::State::NONE) return "none";
		return "???";
	}
}

namespace tr2 {

PooledTileData::PooledTileData(TiledRendererCfg &cfg) : cfg(cfg) {
	available.resize(cfg.maxTiles);
	for (int i=0; i<cfg.maxTiles; i++) available[i] = i;
}
bool PooledTileData::withdrawOne(uint32_t out[1]) {
	if (available.size() == 0) return true;
	out[0] = available.back();
	available.pop_back();
	return false;
}
bool PooledTileData::withdrawFour(uint32_t out[4]) {
	for (int i=0; i<4; i++)
		if (withdrawOne(out+i)) return true;
	return false;
}
bool PooledTileData::depositOne(uint32_t in[1]) {
	available.push_back(in[0]);
	if (available.size() >= cfg.maxTiles)
		return true;
	return false;
}
bool PooledTileData::depositFour(uint32_t in[4]) {
	for (int i=0; i<4; i++)
		if (depositOne(in+i)) return true;
	return false;
}

/* ===================================================
 *
 *
 *                  TileDataLoader
 *
 *
 * =================================================== */

void TileDataLoader::init(
		const std::string& colorDsetPath,
		const std::string& elevDsetPath) {
	internalThread = std::thread(&TileDataLoader::internalLoop, this, colorDsetPath, elevDsetPath);
}

void TileDataLoader::internalLoop(
		const std::string& colorDsetPath,
		const std::string& elevDsetPath) {

	// Load datasets
	DatasetReaderOptions dopts1, dopts2;
	//dopts1.allowInflate = true;
	dopts2.allowInflate = true;
	colorDset = new DatasetReader(colorDsetPath, dopts1);
	elevDset  = new DatasetReader(elevDsetPath,  dopts2);

	colorDset->beginTxn(&color_txn, true);
	elevDset->beginTxn(&elev_txn, true);

	assert(cfg.tileSize == colorDset->tileSize());

	// Create uploader
	myUploadQueue = app->deviceGpu.getQueue(app->queueFamilyGfxIdxs[0], 1);
	myUploader = std::move(Uploader(app, *myUploadQueue));

	colorFormat = Image::Format::RGBA;
	if (colorDset->format() == Image::Format::GRAY or cfg.channels == 1) colorFormat = Image::Format::GRAY;
	assert(cfg.tileSize == colorDset->tileSize());
	colorBuf = Image { (int)colorDset->tileSize(), (int)colorDset->tileSize(), colorFormat };
	elevBuf = Image { (int)elevDset->tileSize(), (int)elevDset->tileSize(), Image::Format::TERRAIN_2x8 };
	colorBuf.alloc();
	elevBuf.alloc();
	if (colorFormat == Image::Format::RGBA)
		for (int y=0; y<cfg.tileSize; y++)
		for (int x=0; x<cfg.tileSize; x++)
			colorBuf.buffer[y*cfg.tileSize*4+x*4+3] = 255;
	memset(elevBuf.buffer, 0, elevBuf.size());

	while (not shouldStop) {
		std::vector<Ask> curAsks;
		{
			std::lock_guard<std::mutex> lck(mtx);
			//std::swap(curAsks, this->asks);
			curAsks = std::move(this->asks);
		}
		if (curAsks.size())
			fmt::print(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] handling {} asks\n", curAsks.size());

		for (; curAsks.size(); curAsks.pop_back()) {
			Ask& ask = curAsks.back();
			Result res;
			res.tiles[0] = res.tiles[1] = res.tiles[2] = res.tiles[3] = nullptr;
			assert(ask.ntiles == 1 or ask.ntiles == 4);
			// Load either one or four.
			if (ask.ntiles == 1) {
				BlockCoordinate bc = ask.tiles[0]->bc;
				dprint(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] loading one ({} {} {})\n", bc.z(), bc.y(), bc.x());

				if (loadTile(ask.tiles[0])) {
					fmt::print(fmt::fg(fmt::color::pink), " - [#Loader::internalLoop] FAILED loading one ({} {} {})\n", bc.z(), bc.y(), bc.x());
					continue;
				} else {
					res.ntiles = 1;
					res.tiles[0] = ask.tiles[0];
					res.parent = ask.parent;
				}
			}
			else {
				assert(ask.parent && "a quad Ask must have a parent");
				BlockCoordinate parent_bc = ask.parent->bc;
				dprint(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] loading quad (parent {} {} {})\n", parent_bc.z(), parent_bc.y(), parent_bc.x());
				bool failed = false;
				for (int i=0; i<4; i++) {
					if (loadTile(ask.tiles[i])) {
						const BlockCoordinate bc = ask.tiles[i]->bc;
						fmt::print(fmt::fg(fmt::color::pink), " - [#Loader::internalLoop] FAILED loading quad (ci {}/{}) ({} {} {})\n", i,4, bc.z(), bc.y(), bc.x());
						failed = true;
						break;
					} else
						res.tiles[i] = ask.tiles[i];
				}

				if (not failed) {
					res.ntiles = 4;
					res.parent = ask.parent;
				} else continue;
			}


			// Lock; push
			{
				std::lock_guard<std::mutex> lck(mtx);
				loadedResults.push_back(res);
			}
		}
		usleep(100'000);
	}

	if (color_txn) colorDset->endTxn(&color_txn);
	if (elev_txn) elevDset->endTxn(&elev_txn);
	if (colorDset) delete colorDset;
	if (elevDset) delete elevDset;
	colorDset = nullptr;
	elevDset = nullptr;

}

TileDataLoader::~TileDataLoader() {
	shouldStop = true;
	if (internalThread.joinable()) internalThread.join();
}
bool TileDataLoader::tileExists(const BlockCoordinate& bc) {
	//#warning "fix me"
	//return true;
	return colorDset->tileExists(bc, color_txn);
}

bool TileDataLoader::pushAsk(const Ask& ask) {
	uint32_t id[4];
	assert(ask.ntiles == 1 or ask.ntiles == 4);
	while (true) {
		//std::lock_guard<std::mutex> lck(mtx);
		if (ask.ntiles == 1 and pooledTileData.withdrawOne(id))
			fmt::print(" - [#loadTile()] failed to withdrawOne (stack empty?)\n");
		else if (ask.ntiles == 4 and pooledTileData.withdrawFour(id))
			fmt::print(" - [#loadTile()] failed to withdrawFour (stack empty?)\n");
		else break;
		usleep(10'000);
	}
	asks.push_back(ask);

	for (int i=0; i<ask.ntiles; i++) ask.tiles[i]->idx = id[i];

	return false;
}
bool TileDataLoader::loadRootTile(Tile* tile) {
	for (int i=0; i<24; i++) {
		if (colorDset->hasLevel(i)) {
			fmt::print(" - [#loadRootTile] found level at {}\n", i);
			uint64_t lvlTlbr[4];
			colorDset->determineLevelAABB(lvlTlbr, i);
			uint64_t w = lvlTlbr[2] - lvlTlbr[0];
			uint64_t h = lvlTlbr[3] - lvlTlbr[1];
			if (w != 1 or h != 1) {
				fmt::print(" - [#loadRootTile] lvl {} had size {} {}, but should be 1x1!\n", i, w,h);
				throw std::runtime_error("bad root lvl size : " + std::to_string(w*h));
				//return true;
			}

			tile->reset(BlockCoordinate { (uint64_t)i, lvlTlbr[1], lvlTlbr[0] }, true);
			std::lock_guard<std::mutex> lck(mtx);
			tile->state = Tile::State::LOADING;
			pushAsk(Ask{
				1,
				{tile,0,0,0},
				nullptr
			});
			return false;
		}
	}
	fmt::print(fmt::fg(fmt::color::red), " - [#loadRootTile] failed to find root tile, very bad!\n");
	return true;
}

bool TileDataLoader::loadTile(Tile* tile) {
	//uint32_t id[1];
	//tile->idx = id[0];

	if (loadColor(tile)) {

		uint64_t tlbr[4] = { tile->bc.x(), tile->bc.y(), tile->bc.x()+1lu, tile->bc.y()+1lu };
		if (colorBuf.channels() == 1) {
			memset(colorBuf.buffer, 0, colorBuf.size());
		} else {
			// set zero, but don't touch alpha
			for (int i=0; i<colorBuf.h; i++)
			for (int j=0; j<colorBuf.w; j++) {
				colorBuf.buffer[j*colorBuf.w*4+0] = 0;
				colorBuf.buffer[j*colorBuf.w*4+1] = 0;
				colorBuf.buffer[j*colorBuf.w*4+2] = 0;
			}
		}
		auto& tex = pooledTileData.texs[tile->idx];
		myUploader.uploadSync(tex, colorBuf.buffer, cfg.tileSize*cfg.tileSize*4, 0);
		//return true;
	}

	if (loadElevAndMeta(tile)) {
		return true;
	}

	dprint(" - [#loadTile()] successfully loaded tile {} {} {} to idx {}\n",
			tile->bc.z(), tile->bc.y(), tile->bc.x(), tile->idx);
	return false;
}

static uint32_t log2_(uint32_t x) {
	uint32_t y = 0;
	while (x>>=1) y++;
	return y;
}

bool TileDataLoader::loadColor(Tile* tile) {
	auto& tex = pooledTileData.texs[tile->idx];

	/*
	uint8_t *img = (uint8_t*)malloc(cfg.tileSize*cfg.tileSize*4);
	free(img);
	for (int y=0; y<cfg.tileSize; y++)
	for (int x=0; x<cfg.tileSize; x++) {
		int yy = 16-1- y / (cfg.tileSize / 16);
		int xx = x / (cfg.tileSize / 16);
		uint8_t val = ((x/16) == 7 and std::abs(y-128) < 100) * 255;
		img[y*cfg.tileSize*4+x*4+0] = val;
		img[y*cfg.tileSize*4+x*4+1] = val;
		img[y*cfg.tileSize*4+x*4+2] = val;
		img[y*cfg.tileSize*4+x*4+3] = 200;
	}
	app->uploader.uploadSync(tex, img, cfg.tileSize*cfg.tileSize*4, 0);
	*/

	//int DatasetReader::fetchBlocks(Image& out, uint64_t lvl, const uint64_t tlbr[4], MDB_txn* txn0) {
	uint64_t tlbr[4] = { tile->bc.x(), tile->bc.y(), tile->bc.x()+1lu, tile->bc.y()+1lu };
	int n_missing = colorDset->fetchBlocks(colorBuf, tile->bc.z(), tlbr, color_txn);
	dprint(" - [#loadColor()] fetched tiles from tlbr {} {} {} {} ({} missing)\n", tlbr[0],tlbr[1],tlbr[2],tlbr[3], n_missing);
	if (n_missing > 0) return true;
	// app->uploader.uploadSync(tex, colorBuf.buffer, cfg.tileSize*cfg.tileSize*4, 0);
	myUploader.uploadSync(tex, colorBuf.buffer, cfg.tileSize*cfg.tileSize*colorBuf.channels(), 0);

	// When loading the tile for first time, fill in whether it can be opened or not.
	//assert(tile->childrenMissing == Tile::MissingStatus::UNKNOWN);
	if (tile->childrenMissing == Tile::MissingStatus::UNKNOWN) {
		if (childrenAreMissing(tile->bc)) {
			fmt::print(" - tile {} {} {} is missing children\n", tile->bc.z(), tile->bc.y(), tile->bc.x());
			tile->childrenMissing = Tile::MissingStatus::MISSING;
		}
		else {
			// fmt::print(" - tile {} {} {} is NOT missing children\n", tile->bc.z(), tile->bc.y(), tile->bc.x());
			tile->childrenMissing = Tile::MissingStatus::NOT_MISSING;
		}
	}

	return false;
}

bool TileDataLoader::loadElevAndMeta(Tile* tile) {

	uint16_t res_ratio = cfg.tileSize / cfg.vertsAlongEdge; // 32 for now
	uint32_t lvlOffset = log2_(res_ratio);
	uint64_t z = tile->bc.z() - lvlOffset;
	uint64_t y = tile->bc.y() >> lvlOffset;
	uint64_t x = tile->bc.x() >> lvlOffset;
	uint64_t tlbr[4] = { x,y, x+1lu,y+1lu };
	int n_missing = elevDset->fetchBlocks(elevBuf, z, tlbr, elev_txn);

	std::vector<float> vertData(cfg.vertsAlongEdge*cfg.vertsAlongEdge*5, 0);

	if (n_missing) {
		// memset(altBuffer.alt, 0, sizeof(altBuffer.alt));
		fmt::print(" - [#loadElev()] for ({} {} {}) use elev tile ({} {} {}) WAS MISSING?\n",
				(uint32_t)(tile->bc.z()),
				(uint32_t)(tile->bc.y()),
				(uint32_t)(tile->bc.x()),
				z,y,x);
	} else {
		//uint32_t y_off = (tile->bc.y() / lvlOffset) % cfg.vertsAlongEdge;
		// uint32_t y_off = cfg.vertsAlongEdge - 1 - ((tile->bc.y() / lvlOffset) % cfg.vertsAlongEdge);
		// uint32_t x_off = (tile->bc.x() / lvlOffset) % cfg.vertsAlongEdge;
		// uint32_t y_off = (tile->bc.y() % res_ratio) * cfg.tileSize / res_ratio;
		uint32_t y_off = (res_ratio - 1 - (tile->bc.y() % res_ratio)) * cfg.tileSize / res_ratio;
		uint32_t x_off = (tile->bc.x() % res_ratio) * cfg.tileSize / res_ratio;
		uint16_t* data = (uint16_t*) elevBuf.buffer;

		int64_t tz = tile->bc.z();
		int64_t ty = tile->bc.y();
		int64_t tx = tile->bc.x();
		float lvlScale = 2. / (1 << tz);
		float ox = tx * lvlScale * 1. - 1.;
		float oy = (ty * lvlScale * 1. - 1.);
		// ox=oy=-1;
		// lvlScale = 2;
		for (int32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
				int32_t ii = ((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx;
				// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

				float xxx_ = static_cast<float>(xx) / static_cast<float>(cfg.vertsAlongEdge-1);
				float yyy_ = static_cast<float>(yy) / static_cast<float>(cfg.vertsAlongEdge-1);
				float xxx = (xxx_ * lvlScale) + ox;
				float yyy = ((1. - yyy_) * lvlScale) + oy;
				vertData[ii*5+0] = xxx;
				vertData[ii*5+1] = yyy;
				vertData[ii*5+2] = (data[(yy+y_off)*elevBuf.w+xx+x_off] / 8.0) / WebMercatorMapScale;
				// vertData[ii*5+2] = 0;
				vertData[ii*5+3] = xxx_;
				vertData[ii*5+4] = yyy_;
				// altBuffer.alt[((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx] = (data[(yy+y_off)*elevBuf.w+xx+x_off] / 8.0) / WebMercatorMapScale;
		}

		unit_wm_to_ecef(vertData.data(), vertData.size()/5, vertData.data(), 5);
		// std::vector<double> tmp1, tmp2;
		// for (float x : vertData) tmp1.push_back(x);
		// tmp2.resize(vertData.size(),0);
		// unit_wm_to_ecef(tmp2.data(), tmp1.size()/3, tmp1.data());
		// vertData.clear();
		// for (double x : tmp2) vertData.push_back(x);

		// fmt::print(" - Corners before load:\n {} norms {}\n", tile->corners, tile->corners.rowwise().norm().transpose());
		tile->corners <<
			vertData[0*5+0], vertData[0*5+1], vertData[0*5+2],
			vertData[(0*cfg.vertsAlongEdge+cfg.vertsAlongEdge-1)*5+0], vertData[(0*cfg.vertsAlongEdge+cfg.vertsAlongEdge-1)*5+1], vertData[(0*cfg.vertsAlongEdge+cfg.vertsAlongEdge-1)*5+2],
			vertData[((cfg.vertsAlongEdge-1)*cfg.vertsAlongEdge+cfg.vertsAlongEdge-1)*5+0], vertData[((cfg.vertsAlongEdge-1)*cfg.vertsAlongEdge+cfg.vertsAlongEdge-1)*5+1], vertData[((cfg.vertsAlongEdge-1)*cfg.vertsAlongEdge+cfg.vertsAlongEdge-1)*5+2],
			vertData[((cfg.vertsAlongEdge-1)*cfg.vertsAlongEdge+0)*5+0], vertData[((cfg.vertsAlongEdge-1)*cfg.vertsAlongEdge+0)*5+1], vertData[((cfg.vertsAlongEdge-1)*cfg.vertsAlongEdge+0)*5+2];
		/*
		fmt::print(" - Corners after load:\n {} norms {}\n", tile->corners, tile->corners.rowwise().norm().transpose());
		fmt::print(" - loaded {} {} {} | {} {}\n", vertData[0], vertData[1], vertData[2], vertData[3], vertData[4]);
		fmt::print("       -> {} {} {} | {} {} to {}\n", vertData[vertData.size()-5], vertData[vertData.size()-4], vertData[vertData.size()-3], vertData[vertData.size()-2], vertData[vertData.size()-1], tile->idx);
		*/

		dprint(" - [#loadElev()] for ({} {} {}) use elev tile ({} {} {} + {} {})\n",
				(uint32_t)(tile->bc.z()),
				(uint32_t)(tile->bc.y()),
				(uint32_t)(tile->bc.x()),
				z,y,x, y_off, x_off);
	}

	// altBuffer.x = tile->bc.x();
	// altBuffer.y = tile->bc.y();
	// altBuffer.z = tile->bc.z();

	// app->uploader.uploadSync(abuf, &altBuffer, sizeof(AltBuffer), 0);
	// myUploader.uploadSync(abuf, &altBuffer, sizeof(AltBuffer), 0);
	// myUploader.uploadSync(sharedTileData.vertsXYZ, vertData.data(), sizeof(float)*vertData.size(), 4*5*tile->idx*cfg.vertsAlongEdge*cfg.vertsAlongEdge);
	myUploader.uploadSync(sharedTileData.vertsXYZs[tile->idx], vertData.data(), sizeof(float)*vertData.size(), 0);
	dprint(" - [#loadElev()] set tidx {} to zyx {} {} {}\n",
			tile->idx,
			(uint32_t)(tile->bc.z()),
			(uint32_t)(tile->bc.y()),
			(uint32_t)(tile->bc.x()));

	return false;
}

// Return true if ALL children missing
bool TileDataLoader::childrenAreMissing(const BlockCoordinate& bc) {
	for (int i=0; i<4; i++) {
		BlockCoordinate child_bc {
			bc.z() + 1,
			bc.y() * 2 + (i / 2),
			bc.x() * 2 + (i == 1 or i == 2)
		};
		bool exists = tileExists(child_bc);
		if (exists) return false;
	}
	return true;
}



/* ===================================================
 *
 *
 *                     Tile
 *
 *
 * =================================================== */


Tile::~Tile() {
	// A tile cannot unload itself (requires PooledTileData)
	assert(not loaded);
	assert(idx == NO_INDEX);
}
bool Tile::unload(PooledTileData& ptd) {
	if (loaded) {
		assert(idx != NO_INDEX);
		ptd.depositOne(&idx);
		loaded = false;
		idx = NO_INDEX;
	}
	return false;
}

void Tile::reset(const BlockCoordinate& bc, bool setCorners) {
	this->bc = bc;

	if (setCorners) {
	auto z = bc.z(), y = bc.y(), x = bc.x();
	//Eigen::Matrix<float, 2,3, Eigen::RowMajor> corners;
	double scale = 2.0 / (1 << z);
	corners.row(0) << ((x  ) * scale) - 1.0, ((y  ) * scale) - 1.0, 0;
	corners.row(1) << ((x+1) * scale) - 1.0, ((y  ) * scale) - 1.0, 0;
	corners.row(2) << ((x+1) * scale) - 1.0, ((y+1) * scale) - 1.0, 0;
	corners.row(3) << ((x  ) * scale) - 1.0, ((y+1) * scale) - 1.0, 0;
	unit_wm_to_ecef(corners.data(), 4, corners.data());
	}

}

void Tile::update(const TileUpdateContext& tuc, Tile* parent) {
	/*
	 * When:
	 *    - INNER
	 *                if children are leaves, they will compute sse.
	 *                if they all have less than some sse, we can close them (send an Ask, goto LOADING+INNER, children goto CLOSING)
	 *    - LEAF
	 *                compute sse, if large enough, ask to open (allocate children, send an Ask, goto OPENING)
	 *    - OPENING, LOADING, or CLOSING
	 *                do nothing.
	 *
	 *
	 *  No special case to prevent closing root: a parent asks for children to close, so its not possible for the root to close.
	 *
	 */
	//fmt::print(" - #update tile {}\n", bc.c);
	if (state == State::INNER) {
		assert(children[0] != nullptr);
		for (int i=0; i<4; i++) children[i]->update(tuc, this);
		if (children[0]->state == State::LEAF) {
			// TODO check sse of all children, maybe close
			bool i_want_to_close = true;
			for (int i=0; i<4; i++) if (children[i]->state != State::LEAF or children[i]->lastSSE > tuc.sseThresholdClose) i_want_to_close = false;

			// Now check our error, to prevent cycling open-close-open-close-...
			lastSSE = computeSSE(tuc);
			i_want_to_close &= lastSSE < tuc.sseThresholdOpen;


			if (i_want_to_close) {
				dprint(fmt::fg(fmt::color::light_yellow), " - #four children okay to close, enqueue read parent {} {} {}\n", bc.z(), bc.y(), bc.x());
				for (int i=0; i<4; i++) children[i]->state = State::CLOSING;
				state = State::LOADING_INNER;
				std::lock_guard<std::mutex> lck(tuc.dataLoader.mtx);
				tuc.dataLoader.pushAsk(TileDataLoader::Ask{
						1,
						{this,nullptr,nullptr,nullptr},
						parent});
				//tuc.dataLoader.loadTile(this);
			}
		}
	}

	else if (state == State::LEAF) {
		lastSSE = computeSSE(tuc);

		if (lastSSE > tuc.sseThresholdOpen) {

			// If we can load children, start opening this tile, otherwise do nothing
			if (childrenMissing == MissingStatus::NOT_MISSING) {
				dprint(fmt::fg(fmt::color::light_yellow), " - #tile ({} {} {}) requesting children (sse {})\n", bc.z(), bc.y(), bc.x(), lastSSE);
				state = State::OPENING;
				if (children[0] == nullptr)
					for (int i=0; i<4; i++) {
						BlockCoordinate child_bc {
							bc.z() + 1,
							bc.y() * 2 + (i / 2),
							bc.x() * 2 + (i == 1 or i == 2)
						};

						// TODO: I might need to reverse the Y check here.
						children[i] = new Tile(child_bc);
						// 00, 10, 11, 01
						if (i==0) {
							children[i]->corners.row(0) = corners.row(0);
							children[i]->corners.row(1) = (corners.row(0) + corners.row(1)) * .5;
							children[i]->corners.row(2) = (corners.row(0) + corners.row(2)) * .5;
							children[i]->corners.row(3) = (corners.row(0) + corners.row(3)) * .5;
						} else if (i==1) {
							children[i]->corners.row(0) = (corners.row(0) + corners.row(1)) * .5;
							children[i]->corners.row(1) = corners.row(1);
							children[i]->corners.row(2) = (corners.row(1) + corners.row(2)) * .5;
							children[i]->corners.row(3) = (corners.row(0) + corners.row(2)) * .5;
						} else if (i==2) {
							children[i]->corners.row(0) = (corners.row(0) + corners.row(2)) * .5;
							children[i]->corners.row(1) = (corners.row(1) + corners.row(2)) * .5;
							children[i]->corners.row(2) = corners.row(2);
							children[i]->corners.row(3) = (corners.row(2) + corners.row(3)) * .5;
						} else if (i==3) {
							children[i]->corners.row(0) = (corners.row(0) + corners.row(3)) * .5;
							children[i]->corners.row(1) = (corners.row(0) + corners.row(2)) * .5;
							children[i]->corners.row(2) = (corners.row(2) + corners.row(3)) * .5;
							children[i]->corners.row(3) = corners.row(3);
						}

					}
				std::lock_guard<std::mutex> lck(tuc.dataLoader.mtx);
				tuc.dataLoader.pushAsk(TileDataLoader::Ask{
						4,
						{children[0],children[1],children[2],children[3]},
						this});
				//tuc.dataLoader.loadTile(this);
			} else
				dprint(fmt::fg(fmt::color::light_yellow), " - #tile ({} {} {}) not opening since children missing (sse {})\n", bc.z(), bc.y(), bc.x(), lastSSE);
		} else
			dprint(fmt::fg(fmt::color::light_yellow), " - #tile ({} {} {}) no change for leaf (sse {})\n", bc.z(), bc.y(), bc.x(), lastSSE);
	}
}

void Tile::render(TileRenderContext& trc) {
	dprint(" - render tile ({} {} {}) (s {}) (idx {})\n", bc.z(),bc.y(),bc.x(), to_string(state), idx);

	if (state == State::INNER or state == State::LOADING_INNER) {
		for (int i=0; i<4; i++) {
			assert(children[i]);
			children[i]->render(trc);
		}
	} else if (state == State::LEAF or state == State::OPENING or state == State::CLOSING) {
		assert(loaded);
		if (loaded) {
			//assert(state == State::LEAF);
			assert(state == State::LEAF or state == State::OPENING or state == State::CLOSING);
			assert(idx != NO_INDEX);
			trc.drawTileIds.push_back(idx);
		}
	}
}

float Tile::computeSSE(const TileUpdateContext& tuc) {

	/*
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
		if (corners2(i,2) > 0 and corners2(i,2) < 1) {
			tl(0) = std::min(tl(0), corners2(i,0));
			tl(1) = std::min(tl(1), corners2(i,1));
			br(0) = std::max(br(0), corners2(i,0));
			br(1) = std::max(br(1), corners2(i,1));
		} else n_invalid++;
	}
	*/


	// TODO This is no where near 'good', but it seems to work okay.
	// The culling is pretty bad, but at least pretty consistent.

	// Eigen::Matrix<float,4,3,Eigen::RowMajor> corners2 = (corners.rowwise().homogeneous() * tuc.mvp.transpose()).rowwise().hnormalized();
	static constexpr float min_p = .0000001f;
	Eigen::Matrix<float,4,4,Eigen::RowMajor> corners1 = (corners.rowwise().homogeneous() * tuc.mvp.transpose());
	// std::cout << " - corners1:\n" << corners1 << "\n";
	int n_invalid0 = 0;
	for (int i=0; i<4; i++) {
		if (corners1(i,3) < min_p) {
			if (corners1(i,3) < 0) n_invalid0++;
			corners1(i,2) = .5 * min_p;
			corners1(i,3) = min_p;
		}
	}
	if (n_invalid0 == 4) return 0;
	Eigen::Matrix<float,4,3,Eigen::RowMajor> corners2 = corners1.rowwise().hnormalized();

	Vector2f tl{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},
			 br{-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
	int n_invalid = 0;
	for (int i=0; i<4; i++) {
		if (corners2(i,2) > 0 and corners2(i,2) < 1) {
			tl(0) = std::min(tl(0), corners2(i,0));
			tl(1) = std::min(tl(1), corners2(i,1));
			br(0) = std::max(br(0), corners2(i,0));
			br(1) = std::max(br(1), corners2(i,1));
		} else {
			n_invalid++;
			// tl(0) = std::min(tl(0), corners2(i,0)/corners(i,2));
			// tl(1) = std::min(tl(1), corners2(i,1)/corners(i,2));
			// br(0) = std::max(br(0), corners2(i,0)/corners(i,2));
			// br(1) = std::max(br(1), corners2(i,1)/corners(i,2));
		}
	}
	// fmt::print(" - n_invald {}\n", n_invalid);
	// fmt::print(" - xsect {} -> {}\n", tl.transpose(), br.transpose());
	// fmt::print(" -  with {} -> {}\n", Vector2f{-1,-1}.transpose(), Vector2f{1,1}.transpose());
	if (n_invalid == 4 or not projection_xsects_ndc_box(tl,br)) return 0;

	float tileGeoError = 2.f / (255.0f * (1 << bc.z()));
	float dist = (tuc.eye.transpose() - corners.colwise().mean()).norm();
	return tileGeoError * tuc.wh(1) / (dist * tuc.two_tan_half_fov_y);
}




/* ===================================================
 *
 *
 *                 TiledRenderer
 *
 *
 * =================================================== */

TiledRenderer::TiledRenderer(TiledRendererCfg& cfg_, BaseVkApp* app) :
	app(app),
	cfg(cfg_),
	pooledTileData(cfg),
	dataLoader(app, sharedTileData, pooledTileData)
{}

void TiledRenderer::init() {

	//dataLoader.init("", "");
	// dataLoader.init("/data/naip/ok/ok16.ft", "/data/elevation/gmted/gmted.ft");
	dataLoader.init(cfg.colorPath, cfg.dtedPath);
	// dataLoader.init("/data/naip/md/md16.ft", "/data/elevation/gmted/gmted.ft");
	// dataLoader.init("/data/naip/mocoNaip/out.ft", "/data/elevation/gmted/gmted.ft");

	// Create shared tile data
	{
		std::vector<float> verts_;
		std::vector<uint8_t> inds_; // Note: index type is uint8_t


		// for (int c=0; c<cfg.maxTiles; c++)
		for (int c=0; c<1; c++)
		for (int yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int xx=0; xx<cfg.vertsAlongEdge; xx++) {
			float x = static_cast<float>(xx) / (cfg.vertsAlongEdge-1);
			float y = static_cast<float>(yy) / (cfg.vertsAlongEdge-1);
			verts_.push_back(x * 2 - 1);
			verts_.push_back(y * 2 - 1);
			verts_.push_back(0);
			verts_.push_back(x);
			// verts_.push_back(1.0f - y);
			verts_.push_back(y);
		}
		int S = cfg.vertsAlongEdge;
		for (int yy=0; yy<cfg.vertsAlongEdge-1; yy++)
		for (int xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
			uint32_t a = ((yy+0)*S) + (xx+0);
			uint32_t b = ((yy+0)*S) + (xx+1);
			uint32_t c = ((yy+1)*S) + (xx+1);
			uint32_t d = ((yy+1)*S) + (xx+0);
			inds_.push_back(a); inds_.push_back(b); inds_.push_back(c);
			inds_.push_back(c); inds_.push_back(d); inds_.push_back(a);
		}

		std::cout << " - verts size: " << verts_.size() << " should be " << 8*8*cfg.maxTiles*5 << "\n";
		sharedTileData.inds.setAsIndexBuffer(sizeof(uint8_t)*inds_.size(), false);
		sharedTileData.inds.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		sharedTileData.numInds = inds_.size();
		app->uploader.uploadSync(sharedTileData.inds, inds_.data(), sizeof(uint8_t)*inds_.size(), 0);

		// sharedTileData.vertsXYZ.setAsVertexBuffer(sizeof(float)*verts_.size(), false);
		// sharedTileData.vertsXYZ.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		// app->uploader.uploadSync(sharedTileData.vertsXYZ, verts_.data(), sizeof(float)*verts_.size(), 0);
		for (int i=0; i<cfg.maxTiles; i++) {
			sharedTileData.vertsXYZs[i].setAsVertexBuffer(sizeof(float)*verts_.size(), false);
			sharedTileData.vertsXYZs[i].create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		}
	}

	// Create resources and descriptors for tile data
	{
		pooledTileData.texs.resize(cfg.maxTiles);
		int N = cfg.vertsAlongEdge * cfg.vertsAlongEdge;
		uint8_t *emptyImage = (uint8_t*)malloc(cfg.tileSize*cfg.tileSize*4);
		memset(emptyImage, 100, cfg.tileSize*cfg.tileSize*4);
		for (int i=0; i<cfg.maxTiles; i++) {
			if (dataLoader.colorFormat == Image::Format::GRAY)
				pooledTileData.texs[i].createAsTexture(app->uploader, cfg.tileSize, cfg.tileSize, vk::Format::eR8Unorm, emptyImage);
			else
				pooledTileData.texs[i].createAsTexture(app->uploader, cfg.tileSize, cfg.tileSize, vk::Format::eR8G8B8A8Unorm, emptyImage);
		}
		free(emptyImage);

		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
			vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, 1 },
			vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, cfg.maxTiles },
			//vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
			//vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 1 },
		};
		vk::DescriptorPoolCreateInfo poolInfo {
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
				cfg.maxTiles,
				(uint32_t)poolSizes.size(), poolSizes.data()
		};
		descPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

		// Bind textures and altBufs. There are only two bindings, but each is an array of length @cfg.maxTiles
		{
			std::vector<vk::DescriptorSetLayoutBinding> bindings;
			// Texture array binding
			bindings.push_back({
					0, vk::DescriptorType::eCombinedImageSampler,
					cfg.maxTiles, vk::ShaderStageFlagBits::eFragment });

			vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
			pooledTileData.descSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

			vk::DescriptorSetAllocateInfo allocInfo {
				*descPool, 1, &*pooledTileData.descSetLayout
			};
			pooledTileData.descSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

			// descSet is allocated, now make the arrays point correctly on the gpu side.
			std::vector<vk::DescriptorImageInfo> i_infos;

			for (int j=0; j<cfg.maxTiles; j++) {
				i_infos.push_back(vk::DescriptorImageInfo{
						*pooledTileData.texs[j].sampler,
						*pooledTileData.texs[j].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
				});
			}


			vk::WriteDescriptorSet writeDesc[1] = {
				{
					*pooledTileData.descSet,
					0, 0, (uint32_t)i_infos.size(),
					vk::DescriptorType::eCombinedImageSampler,
					i_infos.data(),
					nullptr,
					nullptr
				}
			};
			app->deviceGpu.updateDescriptorSets({1, writeDesc}, nullptr);
		}
	}

	// Allocate command buffers
	{
		cmdPool = std::move(app->deviceGpu.createCommandPool(vk::CommandPoolCreateInfo{
					vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
					app->queueFamilyGfxIdxs[0] }));
		cmdBuffers = std::move(app->deviceGpu.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
					*cmdPool,
					vk::CommandBufferLevel::ePrimary,
					//(uint32_t)(app->scNumImages*levels) }));
					(uint32_t)(app->scNumImages) }));
	}

	// Global data (camera etc)
	{
		globalBuffer.setAsUniformBuffer(sizeof(TRGlobalData), true);
		globalBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);
		//camAndMetaBuffer.upload(viewProj, 16*4);

		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex } };

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, 1, bindings };
		globalDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *descPool, 1, &*globalDescSetLayout };
		globalDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		vk::DescriptorBufferInfo binfo {
			*globalBuffer.buffer, 0, VK_WHOLE_SIZE
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
		// std::string vsrcPath = "../src/shaders/tiledRenderer2/1.v.glsl";
		// std::string fsrcPath = "../src/shaders/tiledRenderer2/1.f.glsl";
		// createShaderFromFiles(app->deviceGpu, sharedTileData.pipelineStuff.vs, sharedTileData.pipelineStuff.fs, vsrcPath, fsrcPath);
		createShaderFromSpirv(app->deviceGpu, sharedTileData.pipelineStuff.vs, sharedTileData.pipelineStuff.fs,
				tiledRenderer2_1_v_glsl_len, tiledRenderer2_1_f_glsl_len, tiledRenderer2_1_v_glsl, tiledRenderer2_1_f_glsl);

		sharedTileData.pipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		//VertexInputDescription vertexInputDescription = mldMesh.getVertexDescription();
		MeshDescription md;
		md.posDims = 3;
		md.rows = cfg.vertsAlongEdge*cfg.vertsAlongEdge;
		// md.cols = 3 + 2;
		md.haveUvs = true;
		md.haveNormals = false;
		md.indType = vk::IndexType::eUint8EXT;
		VertexInputDescription vertexInputDescription = md.getVertexDescription();
		plBuilder.init(
				vertexInputDescription,
				vk::PrimitiveTopology::eTriangleList,
				*sharedTileData.pipelineStuff.vs, *sharedTileData.pipelineStuff.fs);

		// Add Push Constants & Set Layouts.
		sharedTileData.pipelineStuff.setLayouts.push_back(*globalDescSetLayout);
		sharedTileData.pipelineStuff.setLayouts.push_back(*pooledTileData.descSetLayout);


		sharedTileData.pipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eFragment,
				0,
				sizeof(TiledRendererPushConstants) });

		sharedTileData.pipelineStuff.build(plBuilder, app->deviceGpu, *app->simpleRenderPass.pass);
	}

	//
	// Caster stuff (1: descriptor set and layout, 2: pipeline stuff)
	//

	// Create descriptor stuff for caster
	{
		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
			vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 2 },
		};
		vk::DescriptorPoolCreateInfo poolInfo {
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
				1,
				(uint32_t)poolSizes.size(), poolSizes.data()
		};
		sharedTileData.casterDescPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

		sharedTileData.casterBuffer.setAsUniformBuffer(sizeof(CasterBuffer), true);
		sharedTileData.casterBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);
		// Initialize caster buffer as all zeros
		void* dbuf = (void*) sharedTileData.casterBuffer.mem.mapMemory(0, sizeof(CasterBuffer), {});
		memset(dbuf, 0, sizeof(CasterBuffer));
		sharedTileData.casterBuffer.mem.unmapMemory();



		// Setup bindings. There is one for the casterData, and one for the casterImages.
		// The casterData is not an array. The casterImages is an array of length two.
		{
			std::vector<vk::DescriptorSetLayoutBinding> bindings;
			// casterData
			bindings.push_back({
					0, vk::DescriptorType::eUniformBuffer,
					1, vk::ShaderStageFlagBits::eVertex });
			// casterImages binding
			bindings.push_back({
					1, vk::DescriptorType::eCombinedImageSampler,
					// 2, vk::ShaderStageFlagBits::eFragment });
					1, vk::ShaderStageFlagBits::eFragment });

			vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
			sharedTileData.casterDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

			vk::DescriptorSetAllocateInfo allocInfo {
				*descPool, 1, &*sharedTileData.casterDescSetLayout
			};
			sharedTileData.casterDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

			// descSet is allocated, now make the arrays point correctly on the gpu side.
			std::vector<vk::DescriptorImageInfo> i_infos;
			std::vector<vk::DescriptorBufferInfo> b_infos;

			b_infos.push_back(vk::DescriptorBufferInfo{
					*sharedTileData.casterBuffer.buffer, 0, VK_WHOLE_SIZE
					});

			// for (int j=0; j<2; j++) {
			for (int j=0; j<1; j++) {
				i_infos.push_back(vk::DescriptorImageInfo{
						*sharedTileData.casterImages[j].sampler,
						*sharedTileData.casterImages[j].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
				});
			}

			std::vector<vk::WriteDescriptorSet> writeDesc = {
				{
					*sharedTileData.casterDescSet,
					0, 0, (uint32_t)b_infos.size(),
					vk::DescriptorType::eUniformBuffer,
					nullptr,
					b_infos.data(),
					nullptr
				},
				/*{
					*sharedTileData.casterDescSet,
					1, 0, (uint32_t)i_infos.size(),
					vk::DescriptorType::eCombinedImageSampler,
					i_infos.data(),
					nullptr,
					nullptr
				}*/
			};
			app->deviceGpu.updateDescriptorSets({(uint32_t)writeDesc.size(), writeDesc.data()}, nullptr);
		}
	}

	// Create pipeline stuff for caster
	{
		PipelineBuilder plBuilder;
		std::string vsrcPath = "../src/shaders/tiledRenderer2/cast.v.glsl";
		std::string fsrcPath = "../src/shaders/tiledRenderer2/cast.f.glsl";
		createShaderFromFiles(app->deviceGpu, sharedTileData.casterPipelineStuff.vs, sharedTileData.casterPipelineStuff.fs, vsrcPath, fsrcPath);
		// createShaderFromSpirv(app->deviceGpu, sharedTileData.casterPipelineStuff.vs, sharedTileData.casterPipelineStuff.fs,
				// tiledRenderer2_cast_v_glsl_len, tiledRenderer2_cast_f_glsl_len, tiledRenderer2_cast_v_glsl, tiledRenderer2_cast_f_glsl);

		sharedTileData.casterPipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		//VertexInputDescription vertexInputDescription = mldMesh.getVertexDescription();
		MeshDescription md;
		md.posDims = 3;
		md.rows = cfg.vertsAlongEdge*cfg.vertsAlongEdge;
		// md.cols = 3 + 2;
		md.haveUvs = true;
		md.haveNormals = false;
		md.indType = vk::IndexType::eUint8EXT;
		VertexInputDescription vertexInputDescription = md.getVertexDescription();
		plBuilder.init(
				vertexInputDescription,
				vk::PrimitiveTopology::eTriangleList,
				*sharedTileData.casterPipelineStuff.vs, *sharedTileData.casterPipelineStuff.fs);

		// Add Push Constants & Set Layouts.
		sharedTileData.casterPipelineStuff.setLayouts.push_back(*globalDescSetLayout);
		sharedTileData.casterPipelineStuff.setLayouts.push_back(*pooledTileData.descSetLayout);

		// For the caster matrices and metadata, we have a third set.
		sharedTileData.casterPipelineStuff.setLayouts.push_back(*sharedTileData.casterDescSetLayout);

		sharedTileData.casterPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eFragment,
				0,
				sizeof(TiledRendererPushConstants) });

		sharedTileData.casterPipelineStuff.build(plBuilder, app->deviceGpu, *app->simpleRenderPass.pass);
	}


	// Find root frast tile, then create the Tile backing it.
	// Done on the current thread, so we synch load it
	{
		//root = &pooledTileData.tiles[0];
		root = new Tile();
		dataLoader.loadRootTile(root);
	}


	drawBuffer.setAsBuffer(sizeof(VkDrawIndexedIndirectCommand) * cfg.maxTiles, true, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer);
	drawBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);

}

void TiledRenderer::update(const RenderState& rs) {
	TileUpdateContext tuc { dataLoader };
	tuc.mvp = Eigen::Map<const RowMatrix4d> { rs.mvp() }.cast<float>();
	Vector3d eyed;
	rs.eyed(eyed.data());
	tuc.eye = eyed.cast<float>();
	tuc.wh = Vector2f { rs.camera->spec().w, rs.camera->spec().h };
	tuc.two_tan_half_fov_y = 2.f * std::tan(rs.camera->spec().vfov * .5f);
	tuc.sseThresholdOpen = 1.1;
	tuc.sseThresholdClose = .45;
	if (0) fmt::print(" - TileUpdateContext:"
			"\n\t{:.4f} {:.4f} {:.4f} {:.4f}"
			"\n\t{:.4f} {:.4f} {:.4f} {:.4f}"
			"\n\t{:.4f} {:.4f} {:.4f} {:.4f}"
			"\n\t{:.4f} {:.4f} {:.4f} {:.4f},"
			"\n  eye {:.4f} {:.4f} {:.4f}, fy {:.4f}, wh {} {}\n",
			tuc.mvp(0,0), tuc.mvp(0,1), tuc.mvp(0,2), tuc.mvp(0,3),
			tuc.mvp(1,0), tuc.mvp(1,1), tuc.mvp(1,2), tuc.mvp(1,3),
			tuc.mvp(2,0), tuc.mvp(2,1), tuc.mvp(2,2), tuc.mvp(2,3),
			tuc.mvp(3,0), tuc.mvp(3,1), tuc.mvp(3,2), tuc.mvp(3,3),
			tuc.eye(0), tuc.eye(1), tuc.eye(2), tuc.two_tan_half_fov_y, tuc.wh(0), tuc.wh(1));


	// Handle any new loaded data
	{
		std::lock_guard<std::mutex> lck(dataLoader.mtx);
		if (dataLoader.loadedResults.size())
			fmt::print(fmt::fg(fmt::color::yellow), " - [#update] handling {} loaded results\n", dataLoader.loadedResults.size());
		for (; dataLoader.loadedResults.size(); dataLoader.loadedResults.pop_back()) {
			auto &res = dataLoader.loadedResults.back();
			Tile* parent = res.parent;
			dprint(fmt::fg(fmt::color::yellow), " - [#update] res (n {})\n", res.ntiles);
			if (res.ntiles == 1) {
				// We loaded either the root, or a parent that will close four children
				if (parent == nullptr) {
					// Root.
					assert(root->state == Tile::State::LOADING or root->state == Tile::State::LOADING_INNER);
					root->state = Tile::State::LEAF;
					root->loaded = true;
				} else {
					// Closing a quad.
					Tile* theParent = res.tiles[0];
					assert(theParent->state == Tile::State::LOADING_INNER);
					theParent->state = Tile::State::LEAF;
					theParent->loaded = true;
					dprint(fmt::fg(fmt::color::yellow), " - [#update] tile ({} {} {}) loaded, closing children\n", theParent->bc.z(),theParent->bc.y(),theParent->bc.x());
					for (int i=0; i<4; i++) {
						dprint(fmt::fg(fmt::color::yellow), " - [#update]     child state {}\n", to_string(theParent->children[i]->state));
						assert(theParent->children[i]->state == Tile::State::CLOSING);
						assert(theParent->children[i]->loaded);
						theParent->children[i]->unload(pooledTileData);
						delete theParent->children[i];
						theParent->children[i] = nullptr;
					}
				}

			} else if (res.ntiles == 4) {
				// We have four children for an opening parent.
				assert(parent);
				assert(parent->state == Tile::State::OPENING);
				assert(parent->loaded);
				dprint(fmt::fg(fmt::color::yellow), " - [#update] tile ({} {} {}) had children loaded, now inner.\n", parent->bc.z(),parent->bc.y(),parent->bc.x());
				parent->unload(pooledTileData);
				parent->state = Tile::State::INNER;
				for (int i=0; i<4; i++) {
					parent->children[i]->state = Tile::State::LEAF;
					parent->children[i]->loaded = true;
				}
			} else {
				throw std::runtime_error("loaded tiles must be 1 or 4");
			}
		}
	}

	root->update(tuc, nullptr);
}

vk::CommandBuffer TiledRenderer::stepAndRender(const RenderState& rs) {
	update(rs);
	return render(rs);
}


void TiledRenderer::setCasterInRenderThread(const CasterWaitingData& cwd) {
	// Update cpu mask
	sharedTileData.casterMask = cwd.mask;

	// Upload texture
	{
		// If texture is new size, must create it then write descSet
		if (sharedTileData.casterImages[0].extent.width != cwd.image.w or sharedTileData.casterImages[0].extent.height != cwd.image.h) {
			fmt::print(" - [setCaster] new image size {} {} {}\n", cwd.image.h, cwd.image.w, cwd.image.channels());
			if (cwd.image.format == Image::Format::GRAY)
				sharedTileData.casterImages[0].createAsTexture(app->uploader, cwd.image.w, cwd.image.h, vk::Format::eR8Unorm, cwd.image.buffer);
			else
				sharedTileData.casterImages[0].createAsTexture(app->uploader, cwd.image.w, cwd.image.h, vk::Format::eR8G8B8A8Unorm, cwd.image.buffer);

			std::vector<vk::DescriptorImageInfo> i_infos = {
				vk::DescriptorImageInfo{
					*sharedTileData.casterImages[0].sampler,
					*sharedTileData.casterImages[0].view,
					vk::ImageLayout::eShaderReadOnlyOptimal
					}};
			vk::WriteDescriptorSet writeDesc = {
				*sharedTileData.casterDescSet,
				1, 0, (uint32_t)i_infos.size(),
				vk::DescriptorType::eCombinedImageSampler,
				i_infos.data(),
				nullptr,
				nullptr
			};
			app->deviceGpu.updateDescriptorSets({1, &writeDesc}, nullptr);
			sharedTileData.casterTextureSet = true;
		} else {
			app->uploader.uploadSync(sharedTileData.casterImages[0], cwd.image.buffer, cwd.image.w*cwd.image.h*cwd.image.channels(), 0);
		}
	}

	// Upload UBO
	{
		CasterBuffer* cameraBuffer = (CasterBuffer*) sharedTileData.casterBuffer.mem.mapMemory(0, sizeof(CasterBuffer), {});
		cameraBuffer->casterMask = cwd.mask;
		if (cwd.haveMatrix1) memcpy(cameraBuffer->casterMatrix   , cwd.casterMatrix1, sizeof(float)*16);
		if (cwd.haveMatrix2) memcpy(cameraBuffer->casterMatrix+16, cwd.casterMatrix2, sizeof(float)*16);
		sharedTileData.casterBuffer.mem.unmapMemory();
	}
}




vk::CommandBuffer TiledRenderer::render(const RenderState& rs) {
	vk::raii::CommandBuffer& cmd = cmdBuffers[rs.frameData->scIndx];
	TileRenderContext trc {
		rs,
		cmd,
		sharedTileData,
		(sharedTileData.numInds),
		{}
	};
	trc.drawTileIds.reserve(cfg.maxTiles);

	cmd.reset();
	cmd.begin({});

	vk::CommandBufferBeginInfo beginInfo { {}, {} };

	vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
	vk::ClearValue clears_[2] = {
		vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.05f,.1f,1.f } } }, // color
		vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 1.f,1.f,1.f,1.f } } }  // depth
	};

	vk::RenderPassBeginInfo rpInfo {
		*app->simpleRenderPass.pass,
			*app->simpleRenderPass.framebuffers[rs.frameData->scIndx],
			aoi,
			{2, clears_}
	};


	cmd.reset();
	cmd.begin(beginInfo);
	cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

	PipelineStuff* thePipelineStuff = 0;
	if (sharedTileData.casterTextureSet and (sharedTileData.casterMask)) {
		// fmt::print(" - using caster pipeline\n");
		thePipelineStuff = &sharedTileData.casterPipelineStuff;
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 1, {1,&*pooledTileData.descSet}, nullptr);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 2, {1,&*sharedTileData.casterDescSet}, nullptr);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipeline);
	} else {
		thePipelineStuff = &sharedTileData.pipelineStuff;
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipelineLayout, 1, {1,&*pooledTileData.descSet}, nullptr);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *thePipelineStuff->pipeline);
	}

	// cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*sharedTileData.vertsXYZ.buffer}, {0u});
	cmd.bindIndexBuffer(*sharedTileData.inds.buffer, {0u}, vk::IndexType::eUint8EXT);
	root->render(trc);

	// Load camera + tile index data
	TRGlobalData trgd;
	uint64_t size = 16*4 + trc.drawTileIds.size() * sizeof(uint32_t); // Don't map/copy entire list of tile ids
	rs.mvpf(trgd.mvp);
	//fmt::print(" - [#render] copying {} bytes to global buffer.\n", size);
	for (int i=0; i<trc.drawTileIds.size(); i++) trgd.drawTileIds[i] = trc.drawTileIds[i];
	void* dbuf = (void*) globalBuffer.mem.mapMemory(0, size, {});
	memcpy(dbuf, &trgd, size);
	globalBuffer.mem.unmapMemory();

	// TODO: Having two shaders probably more efficient
	TiledRendererPushConstants pushc;
	// pushc.grayscale = true;
	pushc.grayscale = false;
	cmd.pushConstants(*thePipelineStuff->pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const TiledRendererPushConstants>{1, &pushc});

	// Now make draw call, with as many instances as tiles to draw
	std::string tiless;
	if (trc.drawTileIds.size() < 12)
		for (int i=0; i<trc.drawTileIds.size(); i++) tiless += std::to_string(trc.drawTileIds[i]) + (i<trc.drawTileIds.size()-1?" ":"");
	else tiless = "...";

	if (frameCnt++ % 30 == 0)
		fmt::print(" - [#TR::render] rendering {} tiles (x{} inds) [{}]\n", trc.drawTileIds.size(), trc.numInds, tiless);

	for (int i=0; i<trc.drawTileIds.size(); i++) {
		cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*sharedTileData.vertsXYZs[trc.drawTileIds[i]].buffer}, {0u});
		// Set the 'firstInstance' as @i, to tell the shader what tile it is (needed for texture)
		cmd.drawIndexed(trc.numInds, 1, 0,0,i);
	}

	cmd.endRenderPass();
	cmd.end();

	return *cmd;
}

}
