#include "tiled_renderer.h"

#include <iostream>
#include <fmt/printf.h>
#include <fmt/color.h>



#if 0
#define dprint(...) fmt::print(__VA_ARGS__)
#else
#define dprint(...)
#endif


namespace {
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
	//dopts2.allowInflate = true;
	colorDset = new DatasetReader(colorDsetPath, dopts1);
	elevDset  = new DatasetReader(elevDsetPath,  dopts2);

	colorDset->beginTxn(&color_txn, true);
	elevDset->beginTxn(&elev_txn, true);

	assert(cfg.tileSize == colorDset->tileSize());

	// Create uploader
	myUploadQueue = app->deviceGpu.getQueue(app->queueFamilyGfxIdxs[0], 1);
	myUploader = std::move(Uploader(app, *myUploadQueue));

	colorFormat = Image::Format::RGBA;
	if (cfg.channels == 1) colorFormat = Image::Format::GRAY;
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
		usleep(33'000);
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

			tile->reset(BlockCoordinate { (uint64_t)i, lvlTlbr[1], lvlTlbr[0] });
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
	myUploader.uploadSync(tex, colorBuf.buffer, cfg.tileSize*cfg.tileSize*4, 0);

	// When loading the tile for first time, fill in whether it can be opened or not.
	//assert(tile->childrenMissing == Tile::MissingStatus::UNKNOWN);
	if (tile->childrenMissing == Tile::MissingStatus::UNKNOWN) {
		if (childrenAreMissing(tile->bc)) {
			fmt::print(" - tile {} {} {} is missing children\n", tile->bc.z(), tile->bc.y(), tile->bc.x());
			tile->childrenMissing = Tile::MissingStatus::MISSING;
		}
		else {
			fmt::print(" - tile {} {} {} is NOT missing children\n", tile->bc.z(), tile->bc.y(), tile->bc.x());
			tile->childrenMissing = Tile::MissingStatus::NOT_MISSING;
		}
	}

	return false;
}

bool TileDataLoader::loadElevAndMeta(Tile* tile) {
	auto& abuf = pooledTileData.altBufs[tile->idx];

	uint16_t res_ratio = cfg.tileSize / cfg.vertsAlongEdge; // 16 for now
	uint32_t lvlOffset = log2_(res_ratio);
	uint64_t z = tile->bc.z() - lvlOffset;
	uint64_t y = tile->bc.y() >> lvlOffset;
	uint64_t x = tile->bc.x() >> lvlOffset;
	uint64_t tlbr[4] = { x,y, x+1lu,y+1lu };
	int n_missing = elevDset->fetchBlocks(elevBuf, z, tlbr, elev_txn);

	if (n_missing) {
		memset(altBuffer.alt, 0, sizeof(float)*64);
	} else {
		//uint32_t y_off = (tile->bc.y() / lvlOffset) % cfg.vertsAlongEdge;
		// uint32_t y_off = cfg.vertsAlongEdge - 1 - ((tile->bc.y() / lvlOffset) % cfg.vertsAlongEdge) all WRONG, I think modulus something else
		// uint32_t x_off = (tile->bc.x() / lvlOffset) % cfg.vertsAlongEdge;
		uint32_t y_off = (tile->bc.y() / res_ratio); FIX ME
		uint32_t x_off = (tile->bc.x() / res_ratio);
		uint16_t* data = (uint16_t*) elevBuf.buffer;
		for (uint32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (uint32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
				altBuffer.alt[(yy*cfg.vertsAlongEdge)+xx] = (data[(yy+y_off)*elevBuf.w+xx+x_off] / 8.0) / WebMercatorMapScale;
				//altBuffer.alt[((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx] = (data[(yy+y_off)*elevBuf.w+xx+x_off] / 8.0) / WebMercatorMapScale;
				//fmt::print(" - alt[{}, {}] {} -> {}\n", y_off, x_off, data[(yy+y_off)*elevBuf.w+xx+x_off], (data[(yy+y_off)*elevBuf.w+xx+x_off] / 8.0) / WebMercatorMapScale);
		}

		dprint(" - [#loadElev()] for ({} {} {}) use elev tile ({} {} {} + {} {})\n",
				(uint32_t)(tile->bc.z()),
				(uint32_t)(tile->bc.y()),
				(uint32_t)(tile->bc.x()),
				z,y,x, y_off, x_off);
	}

	altBuffer.x = tile->bc.x();
	altBuffer.y = tile->bc.y();
	altBuffer.z = tile->bc.z();

	// app->uploader.uploadSync(abuf, &altBuffer, sizeof(AltBuffer), 0);
	myUploader.uploadSync(abuf, &altBuffer, sizeof(AltBuffer), 0);
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

void Tile::reset(const BlockCoordinate& bc) {
	this->bc = bc;
	auto z = bc.z(), y = bc.y(), x = bc.x();
	//Eigen::Matrix<float, 2,3, Eigen::RowMajor> corners;
	double scale = 2.0 / (1 << z);
	corners.row(0) <<
		((x  ) * scale) - 1.0,
		((y  ) * scale) - 1.0,
		0;
	corners.row(1) <<
		((x+1) * scale) - 1.0,
		((y+1) * scale) - 1.0,
		0;

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
						children[i] = new Tile(child_bc);
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
	// std::cout << " - Corners:\n" << corners2 << "\n";
	// std::cout << " - tl: " << tl.transpose() << "\n";
	// std::cout << " - br: " << br.transpose() << "\n";
	/*
	// Two phase algorithm: set invalid rows to a value that does not affect result.
	Vector2f tl_ = corners2.leftCols<2>().colwise().minCoeff();
	Vector2f br_ = corners2.leftCols<2>().colwise().maxCoeff();
	for (int i=0; i<8; i++) if (corners2(i,2) < 0 or corners2(i,2) > 1) corners2.block<1,2>(i,0) = br_;
	//std::cout << " - FixedCorners1:\n" << corners2 << "\n";
	Vector2f tl = corners2.leftCols<2>().colwise().minCoeff();
	for (int i=0; i<8; i++) if (corners2(i,2) < 0 or corners2(i,2) > 1) corners2.block<1,2>(i,0) = tl_;
	//std::cout << " - FixedCorners2:\n" << corners2 << "\n";
	Vector2f br = corners2.leftCols<2>().colwise().maxCoeff();
	*/

	if (n_invalid == 8 or not projection_xsects_ndc_box(tl,br))
		return 0;

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

TiledRenderer::TiledRenderer(BaseVkApp* app) :
	app(app),
	cfg(),
	pooledTileData(cfg),
	dataLoader(app, pooledTileData)
{}

void TiledRenderer::init() {

	//dataLoader.init("", "");
	dataLoader.init("/data/naip/mocoNaip/out.ft", "/data/elevation/gmted/gmted.ft");

	// Create shared tile data
	{
		std::vector<float> verts_;
		std::vector<uint8_t> inds_; // Note: index type is uint8_t

		int c=0;
		for (int yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int xx=0; xx<cfg.vertsAlongEdge; xx++) {
			float x = static_cast<float>(xx) / (cfg.vertsAlongEdge-1);
			float y = static_cast<float>(yy) / (cfg.vertsAlongEdge-1);
			verts_.push_back(x);
			verts_.push_back(y);
			verts_.push_back(c++);
			verts_.push_back(x);
			verts_.push_back(1.0f - y);
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

		sharedTileData.vertsXYI.setAsVertexBuffer(sizeof(float)*verts_.size(), false);
		sharedTileData.inds.setAsIndexBuffer(sizeof(uint8_t)*inds_.size(), false);

		sharedTileData.vertsXYI.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		sharedTileData.inds.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		sharedTileData.numInds = inds_.size();

		//sharedTileData.vertsXY.upload(verts_.data(), sizeof(float)*verts_.size());
		//sharedTileData.inds.upload(inds_.data(), sizeof(uint8_t)*inds_.size());
		app->uploader.uploadSync(sharedTileData.vertsXYI, verts_.data(), sizeof(float)*verts_.size(), 0);
		app->uploader.uploadSync(sharedTileData.inds, inds_.data(), sizeof(uint8_t)*inds_.size(), 0);
	}

	// Create resources and descriptors for tile data
	{
		pooledTileData.texs.resize(cfg.maxTiles);
		pooledTileData.altBufs.resize(cfg.maxTiles);
		int N = cfg.vertsAlongEdge * cfg.vertsAlongEdge;
		uint8_t *emptyImage = (uint8_t*)malloc(cfg.tileSize*cfg.tileSize*4);
		memset(emptyImage, 100, cfg.tileSize*cfg.tileSize*4);
		for (int i=0; i<cfg.maxTiles; i++) {
			pooledTileData.texs[i].createAsTexture(app->uploader, cfg.tileSize, cfg.tileSize, vk::Format::eR8G8B8A8Unorm, emptyImage);
			//pooledTileData.altBufs[i].setAsUniformBuffer(sizeof(float)*N + 3*sizeof(uint32_t), false);
			pooledTileData.altBufs[i].setAsStorageBuffer(sizeof(AltBuffer), false);
			pooledTileData.altBufs[i].create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
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
			// AltBuf array binding
			bindings.push_back({
					//1, vk::DescriptorType::eUniformBuffer,
					1, vk::DescriptorType::eStorageBuffer,
					cfg.maxTiles, vk::ShaderStageFlagBits::eVertex });

			vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
			pooledTileData.descSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

			vk::DescriptorSetAllocateInfo allocInfo {
				*descPool, 1, &*pooledTileData.descSetLayout
			};
			pooledTileData.descSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

			// descSet is allocated, now make the arrays point correctly on the gpu side.
			std::vector<vk::DescriptorBufferInfo> b_infos;
			std::vector<vk::DescriptorImageInfo> i_infos;

			for (int j=0; j<cfg.maxTiles; j++) {
				b_infos.push_back(vk::DescriptorBufferInfo{
						*pooledTileData.altBufs[j].buffer,
						0,
						VK_WHOLE_SIZE
				});
				i_infos.push_back(vk::DescriptorImageInfo{
						*pooledTileData.texs[j].sampler,
						*pooledTileData.texs[j].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
				});
			}


			vk::WriteDescriptorSet writeDesc[2] = {
				{
					*pooledTileData.descSet,
					0, 0, (uint32_t)i_infos.size(),
					vk::DescriptorType::eCombinedImageSampler,
					i_infos.data(),
					nullptr,
					nullptr
				},
				{
					*pooledTileData.descSet,
					1, 0, (uint32_t)b_infos.size(),
					//vk::DescriptorType::eUniformBuffer,
					vk::DescriptorType::eStorageBuffer,
					nullptr,
					b_infos.data(),
					nullptr
				}
			};
			app->deviceGpu.updateDescriptorSets({2, writeDesc}, nullptr);
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
		std::string vsrcPath = "../src/shaders/tiledRenderer/1.v.glsl";
		std::string fsrcPath = "../src/shaders/tiledRenderer/1.f.glsl";
		createShaderFromFiles(app->deviceGpu, sharedTileData.pipelineStuff.vs, sharedTileData.pipelineStuff.fs, vsrcPath, fsrcPath);

		sharedTileData.pipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		//VertexInputDescription vertexInputDescription = mldMesh.getVertexDescription();
		MeshDescription md;
		md.posDims = 3;
		md.rows = cfg.vertsAlongEdge*cfg.vertsAlongEdge;
		md.cols = 3 + 2;
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

		sharedTileData.pipelineStuff.build(plBuilder, app->deviceGpu, *app->simpleRenderPass.pass);
	}

	// Create tile objects
	// Well: I can have the pool include the tile objects,
	// but I think I'd rather create/free tile objects on the fly and pool just the resources.
	/*{
		pooledTileData.tiles.resize(cfg.maxTiles);
		for (int i=0; i<cfg.maxTiles; i++) {
			pooledTileData.tiles[i] = Tile(i);
		}
	}*/

	// Find root frast tile, then create the Tile backing it.
	// Done on the current thread, so we synch load it
	{
		//root = &pooledTileData.tiles[0];
		root = new Tile();
		dataLoader.loadRootTile(root);
	}

}

void TiledRenderer::update(const RenderState& rs) {
	TileUpdateContext tuc { dataLoader };
	tuc.mvp = Eigen::Map<const RowMatrix4d> { rs.mvp() }.cast<float>();
	Vector3d eyed;
	rs.eyed(eyed.data());
	tuc.eye = eyed.cast<float>();
	tuc.wh = Vector2f { rs.camera->spec().w, rs.camera->spec().h };
	tuc.two_tan_half_fov_y = 2.f * std::tan(rs.camera->spec().vfov * .5f);
	tuc.sseThresholdOpen = 1.7;
	tuc.sseThresholdClose = .8;
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

	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedTileData.pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedTileData.pipelineStuff.pipelineLayout, 1, {1,&*pooledTileData.descSet}, nullptr);


	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedTileData.pipelineStuff.pipeline);
	cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*sharedTileData.vertsXYI.buffer}, {0u});
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

	// Now make draw call, with as many instances as tiles to draw
	std::string tiless;
	if (trc.drawTileIds.size() < 12)
		for (int i=0; i<trc.drawTileIds.size(); i++) tiless += std::to_string(trc.drawTileIds[i]) + (i<trc.drawTileIds.size()-1?" ":"");
	else tiless = "...";
	dprint(" - [#TR::render] rendering {} tiles (x{} inds) [{}]\n", trc.drawTileIds.size(), trc.numInds, tiless);
	//trc.drawTileIds = {trc.drawTileIds.back()};
	cmd.drawIndexed(trc.numInds, trc.drawTileIds.size(), 0,0,0);

	cmd.endRenderPass();
	cmd.end();

	return *cmd;
}

