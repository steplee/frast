#include "tiled_renderer.h"

#include <fmt/printf.h>
#include <fmt/color.h>


constexpr static uint32_t NO_INDEX = 999999;


namespace {
	bool box_contains(const Vector2f& a, const Vector2f& b) {
		return (a(0) > -1 or b(0) > -1)
			and (a(1) > -1 or b(1) > -1)
			and (a(0) < 1 or b(0) < 1)
			and (a(1) < 1 or b(1) < 1);
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
	colorDset = new DatasetReader(colorDsetPath, dopts1);
	elevDset  = new DatasetReader(elevDsetPath,  dopts2);

	assert(cfg.tileSize == colorDset->tileSize());

	// Create uploader
	myUploadQueue = app->deviceGpu.getQueue(app->queueFamilyGfxIdxs[0], 1);
	myUploader = std::move(Uploader(app, *myUploadQueue));

	colorFormat = Image::Format::RGBA;
	if (cfg.channels == 1) colorFormat = Image::Format::GRAY;
	colorBuf = Image { (int)cfg.tileSize, (int)cfg.tileSize, colorFormat };
	colorBuf.alloc();
	if (colorFormat == Image::Format::RGBA)
		for (int y=0; y<cfg.tileSize; y++)
		for (int x=0; x<cfg.tileSize; x++)
			colorBuf.buffer[y*cfg.tileSize*4+x*4+3] = 255;

	while (not shouldStop) {
		std::vector<Ask> asks;
		{
			std::lock_guard<std::mutex> lck(mtx);
			std::swap(asks, this->asks);
		}
		fmt::print(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] handling {} asks\n", asks.size());
		for (; asks.size(); asks.pop_back()) {
			Ask& ask = asks.back();
			Result res;
			res.tiles[0] = res.tiles[1] = res.tiles[2] = res.tiles[3] = nullptr;
			// Load either one or four.
			if (ask.ntiles == 1) {
				BlockCoordinate bc = ask.tiles[0]->bc;
				fmt::print(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] loading one ({} {} {})\n", bc.z(), bc.y(), bc.x());

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
				BlockCoordinate bc = ask.parent->bc;
				fmt::print(fmt::fg(fmt::color::light_green), " - [#Loader::internalLoop] loading quad (parent {} {} {})\n", bc.z(), bc.y(), bc.x());
				for (int i=0; i<4; i++) {
				}
			}


			// Lock; push
			{
				std::lock_guard<std::mutex> lck(mtx);
				loadedResults.push_back(res);
			}
		}
		sleep(1);
	}

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
	return colorDset->tileExists(bc, nullptr);
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

			tile->bc = BlockCoordinate { (uint64_t)i, lvlTlbr[1], lvlTlbr[0] };
			//if (loadTile(tile)) throw std::runtime_error("failed to load root tile!");
			std::lock_guard<std::mutex> lck(mtx);
			tile->state = Tile::State::LOADING;
			asks.push_back(Ask{
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
	uint32_t id[1];
	if (pooledTileData.withdrawOne(id)) {
		fmt::print(" - [#loadTile()] failed to withdrawOne (stack empty?)\n");
		return true;
	}
	tile->idx = id[0];

	if (loadColor(tile)) {
		return true;
	}

	if (loadElevAndMeta(tile)) {
		return true;
	}

	fmt::print(" - [#loadTile()] successfully loaded tile {} {} {} to idx {}\n",
			tile->bc.z(), tile->bc.y(), tile->bc.x(), tile->idx);
	return false;
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
	int n_missing = colorDset->fetchBlocks(colorBuf, tile->bc.z(), tlbr, nullptr);
	fmt::print(" - [#loadColor()] fetched tiles from tlbr {} {} {} {}\n", tlbr[0],tlbr[1],tlbr[2],tlbr[3]);
	if (n_missing > 0) return true;
	// app->uploader.uploadSync(tex, colorBuf.buffer, cfg.tileSize*cfg.tileSize*4, 0);
	myUploader.uploadSync(tex, colorBuf.buffer, cfg.tileSize*cfg.tileSize*4, 0);

	return false;
}

bool TileDataLoader::loadElevAndMeta(Tile* tile) {
	auto& abuf = pooledTileData.altBufs[tile->idx];

	altBuffer.x = tile->bc.x();
	altBuffer.y = tile->bc.y();
	altBuffer.z = tile->bc.z();
	memset(altBuffer.alt, 0, sizeof(float)*64);

	// app->uploader.uploadSync(abuf, &altBuffer, sizeof(AltBuffer), 0);
	myUploader.uploadSync(abuf, &altBuffer, sizeof(AltBuffer), 0);

	return false;
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

void Tile::update(const CameraFrustumData& cam) {
	/*
	 *
	 * When:
	 *    - INNER
	 *                if children are leaves, they will compute sse.
	 *                If they all have less than some sse, we can close them (send an Ask, goto CLOSING)
	 *    - LEAF
	 *                compute sse, if large enough, ask to open (allocate children, send an Ask, goto OPENING)
	 *    - OPENING, LOADING, or CLOSING
	 *                do nothing.
	 *
	 *
	 */
	fmt::print(" - #update tile {}\n", bc.c);
	if (state == State::INNER) {
		assert(children[0] != nullptr);
		for (int i=0; i<4; i++) children[i]->update(cam);
		if (children[0]->state == State::LEAF) {
			// TODO check sse of all children, maybe close
		}
		return;
	}

	if (state == State::LEAF) {
		//float sse = computeSSE(
	}
}

void Tile::render(TileRenderContext& trc) {
	fmt::print(" - render tile {}\n", bc.c);

	if (loaded) {
		assert(state == State::LEAF);
		assert(idx != NO_INDEX);
		trc.drawTileIds.push_back(idx);
	}
	//cmd.drawIndexed(trc.numInds, 1, 0, 0, 0);
}

float Tile::computeSSE(const CameraFrustumData& cam) {
	Eigen::Matrix<float,2,4,Eigen::RowMajor> corners1;
	corners1.topLeftCorner<2,3>() = corners;
	corners1.topRightCorner<2,1>().setConstant(1.f);
	Eigen::Matrix<float,2,3,Eigen::RowMajor> corners2 = (corners1 * cam.mvp.transpose()).rowwise().hnormalized();
	if (not box_contains(corners2.block<1,2>(0,0).transpose(), corners2.block<1,2>(1,0).transpose()))
		return 0;
	
	float tileGeoError = 255;
	float dist = (cam.eye.transpose() - corners.colwise().mean()).norm();
	return tileGeoError * cam.wh(1) / (dist * cam.two_tan_half_fov_y);
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
			0, vk::DescriptorType::eUniformBuffer,
			1, vk::ShaderStageFlagBits::eVertex } };

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, 1, bindings };
		globalDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *descPool, 1, &*globalDescSetLayout };
		globalDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		vk::DescriptorBufferInfo binfo {
			*globalBuffer.buffer, 0, sizeof(TRGlobalData)
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
		root = new Tile(0);
		dataLoader.loadRootTile(root);
	}

}

void TiledRenderer::update(const RenderState& rs) {
	CameraFrustumData camd;
	camd.mvp = Eigen::Map<const RowMatrix4d> { rs.mvp() }.cast<float>();
	Vector3d eyed;
	rs.eyed(eyed.data());
	camd.eye = eyed.cast<float>();
	camd.wh = Vector2f { rs.camera->spec().w, rs.camera->spec().h };
	camd.two_tan_half_fov_y = 2.f * std::tan(rs.camera->spec().vfov * .5f);

	// Handle any new loaded data
	{
		std::lock_guard<std::mutex> lck(dataLoader.mtx);
		fmt::print(fmt::fg(fmt::color::yellow), " - [#update] handling {} loaded results\n", dataLoader.loadedResults.size());
		for (; dataLoader.loadedResults.size(); dataLoader.loadedResults.pop_back()) {
			auto &res = dataLoader.loadedResults.back();
			Tile* parent = res.parent;
			if (res.ntiles == 1) {
				// We loaded either the root, or a parent that will close four children
				if (parent == nullptr) {
					// Root.
					assert(root->state == Tile::State::LOADING);
					root->state = Tile::State::LEAF;
					root->loaded = true;
				} else {
					// Closing a quad.
					parent->state = Tile::State::LEAF;
					parent->loaded = true;
					for (int i=0; i<4; i++) {
						assert(parent->children[i]->state == Tile::State::LEAF);
						assert(parent->children[i]->loaded);
						parent->children[i]->unload(pooledTileData);
						delete parent->children[i];
					}
				}

			} else if (res.ntiles == 4) {
				// We have four children for an opening parent.
				assert(parent->opening);
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

	root->update(camd);
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
	memcpy(trgd.drawTileIds, trc.drawTileIds.data(), trc.drawTileIds.size()*sizeof(uint32_t));
	void* dbuf = (void*) globalBuffer.mem.mapMemory(0, 16*4, {});
	memcpy(dbuf, &trgd, size);
	globalBuffer.mem.unmapMemory();

	// Now make draw call, with as many instances as tiles to draw
	fmt::print(" - [#TR::render] rendering {} tiles (x{} inds)\n", trc.drawTileIds.size(), trc.numInds);
	cmd.drawIndexed(trc.numInds, trc.drawTileIds.size(), 0,0,0);

	cmd.endRenderPass();
	cmd.end();

	return *cmd;
}

