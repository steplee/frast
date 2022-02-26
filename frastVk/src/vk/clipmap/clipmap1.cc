#include "clipmap1.h"
#include <iostream>
#include <unistd.h>


namespace {
	void upload_test_verts_and_tex(
			ResidentBuffer& altBuf,
			ResidentImage& tex,
			Uploader& uploader,
			int pixelsPerTile, int tilesPerLevel, int vertsAlongEdge) {

		// Texture.
		int pixelsAlongEdge = pixelsPerTile * tilesPerLevel;
		std::vector<uint8_t> texData(pixelsAlongEdge*pixelsAlongEdge*4);
		for (int y=0; y<pixelsAlongEdge; y++)
			for (int x=0; x<pixelsAlongEdge; x++) {
				for (int c=0; c<3; c++)
					texData[y*pixelsAlongEdge*4+x*4+c] = ((y/32+x/32) % 2) * 100 + 155;
				//texData[y*pixelsAlongEdge*4+x*4+3] = ((y/32+x/32)%2) * 100 + 155;
				texData[y*pixelsAlongEdge*4+x*4+3] = 200;
			}
		printf(" - uploading test texture.\n");
		//tex.createAsTexture(app->uploader, pixelsAlongEdge,pixelsAlongEdge, vk::Format::eR8G8B8A8Unorm, texData.data());
		uploader.uploadSync(tex, texData.data(), texData.size()*sizeof(uint8_t), 0);

		// AltBuf
		using altType = float;
		std::vector<altType> altData;
		float f = 0.f;
		//for (int i=0; i<512; i++) altData.push_back(i * .01f);
		for (int y=0; y<vertsAlongEdge; y++)
			for (int x=0; x<vertsAlongEdge; x++) {
				//altData[y*vertsAlongEdge+x] = ((float)x / vertsAlongEdge) * 8 * 10000.f;
				//altData[y*vertsAlongEdge+x] = (y*vertsAlongEdge+x) * 1;
				altData.push_back(f+ 2.1f * (rand()%99999)/99999.f);
				f += .01f ;
				//altData[y*vertsAlongEdge+x] = 1;
				//altData[y*vertsAlongEdge+x] = 0;
			}
		printf(" - uploading test altBuf with %d.\n", altData.size());
		uploader.uploadSync(altBuf, altData.data(), altData.size()*sizeof(altType), 0);

	}
}

void Ask::setFromCamera(Camera* cam) {
	pos[0] = cam->viewInv()[0*4+3];
	pos[1] = cam->viewInv()[1*4+3];
	pos[2] = cam->viewInv()[2*4+3];
}

ClipMapRenderer1::ClipMapRenderer1(VkApp* app_) :
	app(app_),
	//ClipMapConfig(int levels, int tilesPerLevel, int pixPer, int vertsPer)
	cfg(4, 4, 256, 4) {

	dataLoader.init(cfg,
			"/data/naip/mocoNaip/out.ft",
			"/data/elevation/gmted/gmted.ft");
	loaderThread = std::thread(&ClipMapRenderer1::loaderLoop, this);
}
ClipMapRenderer1::~ClipMapRenderer1() {
	doStop_ = true;
	askCv.notify_one();
	if (loaderThread.joinable()) loaderThread.join();

	// ~VkApp() calls waitIdle. We must ensure this queue has no submits left first though
	cmUploadQueue = nullptr;
}

vk::CommandBuffer ClipMapRenderer1::stepAndRender(RenderState& rs, FrameData& fd, Camera* cam) {

	// If time, request new data in bg thread
	if (true) {
		std::unique_lock<std::mutex> lck(askMtx);
		haveAsk = true;
		currentAsk.setFromCamera(cam);
		lck.unlock();
		askCv.notify_one();
	}

	{
		std::lock_guard<std::mutex> lck{askMtx};
		if (dataReadIdx != dataWriteIdx) {
			dataReadIdx = (dataReadIdx + 1) % 2;
		}
	}

	return render(rs, fd, cam);
}

vk::CommandBuffer ClipMapRenderer1::render(RenderState& rs, FrameData& fd, Camera* cam) {
	MultiLevelData &mld = mlds[dataReadIdx];

	{
		void* dbuf = (void*) camAndMetaBuffer.mem.mapMemory(0, 16*4+4*3, {});

		// WARNING ERROR TODO:
		// No solution: working at any fixed float32 scale is broken in both the CPU matrix stack AND the shader code.
		// There is no general way around it.
		// Best solution: adapt the coordinate system to the local loaded data. That is probably good enough.
		// On top of that: use doubles until ready to copy to shader, then convert to float
		//
		// There are strange artifacts due to floats having better res closer to 0.
		// Subtracting at an origin 1e-2 with a 1e-9 position delta for example is never good, for example
		alignas(16) double old_pos[3] = {
			cam->viewInv()[0*4+3],
			cam->viewInv()[1*4+3],
			cam->viewInv()[2*4+3] };
		alignas(16) double new_pos[3] = { old_pos[0] - mld.ctr_x, old_pos[1] - mld.ctr_y, old_pos[2] };
		cam->setPosition(new_pos);
		rs.frameBegin();
		/*
		alignas(16) float shift_loaded_ctr[16] = {
			1, 0, 0, mld.ctr_x,
			0, 1, 0, mld.ctr_y,
			0, 0, 1, 0,
			0, 0, 0, 1 };
		rs.mstack.push(shift_loaded_ctr);
		*/

		printf(" - Mvp:\n");
		for (int i=0; i<4; i++) {
			for (int j=0; j<4; j++) printf(" %f", rs.mvp()[i*4+j]);
			printf("\n");
		}

		float mvpf[16];
		rs.mvpf(mvpf);
		//memcpy(dbuf, rs.mvp(), 16*4);
		memcpy(dbuf, mvpf, 16*4);
		uint32_t lvl_x_y[3*4];
		((uint32_t*)lvl_x_y)[0] = cfg.levels-1;
		//((float*)   lvl_x_y)[1] = mld.ctr_x;
		//((float*)   lvl_x_y)[2] = mld.ctr_y;
		((float*)   lvl_x_y)[1] = 0;
		((float*)   lvl_x_y)[2] = 0;
		memcpy((void*)((uint8_t*)dbuf+16*4), lvl_x_y, 3*4);
		camAndMetaBuffer.mem.unmapMemory();

		cam->setPosition(old_pos);

		//printf(" - meta ctr at %f %f\n", ((float*)lvl_x_y)[1], ((float*) lvl_x_y)[2]);
	}

	{
		vk::raii::CommandBuffer& cmd = mld.cmdBufs[fd.scIndx];
		return *cmd;
	}

}

void ClipMapRenderer1::loaderLoop() {
	cmUploadQueue = app->deviceGpu.getQueue(app->queueFamilyGfxIdxs[0], 1);
	cmUploader = std::move(Uploader(app, *cmUploadQueue));

	while (true) {
		std::unique_lock<std::mutex> lck{askMtx};
		askCv.wait(lck, [this](){ return haveAsk or doStop_; });
		Ask ask = currentAsk;
		lck.unlock();
		printf(" - [loaderThread] awoke.\n");

		if (doStop_) return;

		// Load it.
		// Don't block the render thread while we do.
		{
			MultiLevelData& mld = mlds[(dataWriteIdx + 1) % 2];
			for (int j=0; j<cfg.levels; j++) {
				ResidentImage &rimg = mld.images[j];
				ResidentBuffer &rbuf = mld.altBufs[j];
				upload_test_verts_and_tex(rbuf, rimg, cmUploader, cfg.pixelsPerTile, cfg.tilesPerLevel, cfg.vertsAlongEdge);
			}

			dataLoader.load(ask);
			mld.ctr_x = dataLoader.loadedData.ctr_x;
			mld.ctr_y = dataLoader.loadedData.ctr_y;
		}

		// Increment, which will also notify other thread.
		{
			printf(" - [loaderThread] Incrementing dataWriteIdx from %d\n", dataWriteIdx);
			askMtx.lock();
			dataWriteIdx = (dataWriteIdx + 1) % 2;
			askMtx.unlock();
		}

		sleep(1);
	}
}


void ClipMapRenderer1::init() {
	assert(app);
	assert(app->scNumImages > 0);


	// Create camera buffer and setup globalDescSet. It contains:
	//      1) Camera matrix (4x4)
	//      2) Level, X, and Y offset of all vertices.
	{
		float viewProj[16];
		for (int i=0; i<16; i++) viewProj[i] = i % 5 == 0;
		camAndMetaBuffer.setAsUniformBuffer(16*4+3*4, true);
		camAndMetaBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);
		camAndMetaBuffer.upload(viewProj, 16*4);
		float lvl_x_y[3] = {0,0,0};
		((uint32_t*)lvl_x_y)[0] = cfg.levels-1;
		camAndMetaBuffer.upload(lvl_x_y, 3*4, 16*4);

		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0, vk::DescriptorType::eUniformBuffer,
			1, vk::ShaderStageFlagBits::eVertex } };

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, 1, bindings };
		globalDescLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *app->descPool, 1, &*globalDescLayout };
		globalDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		vk::DescriptorBufferInfo binfo {
			*camAndMetaBuffer.buffer, 0, 16*4 + 3*4
		};
		vk::WriteDescriptorSet writeDesc[1] = { {
			*globalDescSet,
				0, 0, 1,
				vk::DescriptorType::eUniformBuffer,
				nullptr, &binfo, nullptr } };
		app->deviceGpu.updateDescriptorSets({1, writeDesc}, nullptr);
	}

	commandPool = std::move(app->deviceGpu.createCommandPool(vk::CommandPoolCreateInfo{
				vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				app->queueFamilyGfxIdxs[0] }));

	// Compute geometry.
	{
		lvlIndOffset.clear();
		//std::vector<float> verts2d;
		std::vector<float> verts3d;
		std::vector<float> uvs;
		std::vector<uint32_t> inds;
		int vertOffset = 0;
		int nv = 0;

		for (int yy=0; yy<cfg.vertsAlongEdge; yy++) {
			for (int xx=0; xx<cfg.vertsAlongEdge; xx++) {
				float u = ((float)xx) / (cfg.vertsAlongEdge - 1);
				float v = ((float)yy) / (cfg.vertsAlongEdge - 1);
				//float x = 1.f * (((float)xx) / (cfg.vertsAlongEdge ) - .5f);
				//float y = 1.f * (((float)yy) / (cfg.vertsAlongEdge ) - .5f);
				float x = 1.f * (((float)xx) / (cfg.vertsAlongEdge-1.0f) - .5f);
				float y = 1.f * (((float)yy) / (cfg.vertsAlongEdge-1.0f) - .5f);
				//float x = 1.f * (((float)xx+.5f) / (cfg.vertsAlongEdge+.5f)) - .5f;
				//float y = 1.f * (((float)yy+.5f) / (cfg.vertsAlongEdge+.5f)) - .5f;

				// If level is zero, make full grid
				//verts2d.push_back(x); verts2d.push_back(y);
				verts3d.push_back(x); verts3d.push_back(y); verts3d.push_back(nv++);
				uvs.push_back(u); uvs.push_back(v);
			}
		}

		// Update: only make two sets: one with filled center (for deepest level) and one with hole in middle (all others)
		//for (int lvl=0; lvl<levels; lvl++) {
		for (int lvl=0; lvl<std::min(cfg.levels,2u); lvl++) {
			lvlIndOffset.push_back(inds.size());

			for (int yy=0; yy<cfg.vertsAlongEdge-1; yy++) {
				for (int xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
					uint32_t a = vertOffset + yy*cfg.vertsAlongEdge + xx;
					uint32_t b = vertOffset + yy*cfg.vertsAlongEdge + xx + 1;
					uint32_t c = vertOffset + (yy+1)*cfg.vertsAlongEdge + xx + 1;
					uint32_t d = vertOffset + (yy+1)*cfg.vertsAlongEdge + xx;
					if (0) std::swap(a,b);

					// If level is zero, make full grid
					//if (lvl == 0) {
					if (lvl != 0) {
						inds.push_back(a); inds.push_back(b); inds.push_back(c);
						inds.push_back(c); inds.push_back(d); inds.push_back(a);
					}
					// Otherwise, cut-out interior.
					// For now the tilesPerLevel must be three, and the inner tile should be cut out
					else {
						int tile_x = (xx+0) / cfg.vertsPerTileEdge;
						int tile_y = (yy+0) / cfg.vertsPerTileEdge;
						if ((cfg.tilesPerLevel == 3 and (tile_x != 1 or tile_y != 1)) or
						    (cfg.tilesPerLevel == 4 and (tile_x == 0 or tile_x == 3 or tile_y == 0 or tile_y == 3))
							) {
							inds.push_back(a); inds.push_back(b); inds.push_back(c);
							inds.push_back(c); inds.push_back(d); inds.push_back(a);
						}
					}
				}
			}
		}

		// Push again to simplify draw logic (we can treat last as any other)
		lvlIndOffset.push_back(inds.size());

		//mldMesh.fill(2, verts2d, uvs, {}, inds);
		mldMesh.fill(3, verts3d, uvs, {}, inds);
		mldMesh.createAndUpload(app->uploader);

	}

	// Create mlds:
	//		1) Allocate textures and buffers, 2x for each mld
	//		2) Allocate 2 descriptor sets for each mld, bind resources
	//		3) Create pipelines
	//		4) Allocate & record commands
	//
	// Note: Since I'm not doing any frustum culling, I re-use CommandBuffers for each frame.
	//       To do frustum culling you'd need to record a cmdbuf every frame or use indirect draws.
	//
	// Note: It'd be better to have several texture, but just one altBuf. But I don't do that here.
	//

	for (int ii=0; ii<2; ii++) {
		MultiLevelData& mld = mlds[ii];


		// (1)
		for (int j=0; j<cfg.levels; j++) {
			mld.images.push_back(ResidentImage{});
			mld.altBufs.push_back(ResidentBuffer{});

			ResidentImage &rimg = mld.images.back();
			ResidentBuffer &rbuf = mld.altBufs.back();

			//std::vector<uint8_t> t(levelSize*levelSize*4, 0);
			rimg.createAsTexture(app->uploader, cfg.pixelsAlongEdge, cfg.pixelsAlongEdge, vk::Format::eR8G8B8A8Unorm, nullptr);

			// The altBuf has format: lvl, x, y, z, alt0 ... altN
			// lvl is uint8_t, x&y float, alt uint16_t
			//uint64_t len = 1 + 2*sizeof(float) + mldMesh.rows * sizeof(uint16_t);
			//uint64_t len = 289 * sizeof(uint16_t);
			uint64_t len = cfg.vertsAlongEdge * cfg.vertsAlongEdge * sizeof(float);
			rbuf.setAsUniformBuffer(len);
			//std::cout << " - rbuf size: " << rbuf.size << "\n";
			rbuf.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
			upload_test_verts_and_tex(rbuf, rimg, app->uploader, cfg.pixelsPerTile, cfg.tilesPerLevel, cfg.vertsAlongEdge);
		}

		// (2)
		{
			vk::DescriptorPoolSize poolSizes[2] = {
				vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, (uint32_t)(20) },
				vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, (uint32_t)(20) },
			};
			vk::DescriptorPoolCreateInfo poolInfo {
				vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
					(uint32_t)(2*(cfg.levels+1)),
					2, poolSizes
			};
			mld.descPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

			std::vector<vk::DescriptorSetLayoutBinding> bindings;
			// Texture array binding
			bindings.push_back({
					0, vk::DescriptorType::eCombinedImageSampler,
					cfg.levels, vk::ShaderStageFlagBits::eFragment });
			// AltBuf array binding
			bindings.push_back({
					1, vk::DescriptorType::eUniformBuffer,
					cfg.levels, vk::ShaderStageFlagBits::eVertex });

			if (ii == 0) {
				// Only create DescriptorSetLayout once.
				vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
				mldDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));
			}

			vk::DescriptorSetAllocateInfo allocInfo {
				*mld.descPool, 1, &*mldDescSetLayout
			};
			mld.descSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

			// descSet is allocated, now make the arrays point correctly on the gpu side.
			std::vector<vk::DescriptorBufferInfo> b_infos;
			std::vector<vk::DescriptorImageInfo> i_infos;

			for (int lvl=0; lvl<cfg.levels; lvl++) {
				b_infos.push_back(vk::DescriptorBufferInfo{
						*mld.altBufs[lvl].buffer,
						0,
						VK_WHOLE_SIZE
				});
				i_infos.push_back(vk::DescriptorImageInfo{
						*mld.images[lvl].sampler,
						*mld.images[lvl].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
				});
			}
		
			vk::WriteDescriptorSet writeDesc[2] = {
				{
					*mld.descSet,
					0, 0, (uint32_t)i_infos.size(),
					vk::DescriptorType::eCombinedImageSampler,
					i_infos.data(),
					nullptr,
					nullptr
				},
				{
					*mld.descSet,
					1, 0, (uint32_t)b_infos.size(),
					vk::DescriptorType::eUniformBuffer,
					nullptr,
					b_infos.data(),
					nullptr
				}
			};
			app->deviceGpu.updateDescriptorSets({2, writeDesc}, nullptr);
		}

		// (3)
		if (ii == 0) {
			PipelineBuilder plBuilder;
			std::string vsrcPath = "../src/shaders/clipmap/1.v.glsl";
			std::string fsrcPath = "../src/shaders/clipmap/1.f.glsl";
			createShaderFromFiles(app->deviceGpu, mldPipelineStuff.vs, mldPipelineStuff.fs, vsrcPath, fsrcPath);

			mldPipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
			VertexInputDescription vertexInputDescription = mldMesh.getVertexDescription();
			plBuilder.init(
					vertexInputDescription,
					vk::PrimitiveTopology::eTriangleList,
					*mldPipelineStuff.vs, *mldPipelineStuff.fs);

			// Add Push Constants & Set Layouts.
			mldPipelineStuff.setLayouts.push_back(*globalDescLayout);
			mldPipelineStuff.setLayouts.push_back(*mldDescSetLayout);
			mldPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eVertex,
				0,
				sizeof(MldPushConstants) });

			mldPipelineStuff.build(plBuilder, app->deviceGpu, *app->simpleRenderPass.pass);
		}

		// (4)
		mld.cmdBufs = std::move(app->deviceGpu.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
					*commandPool,
					vk::CommandBufferLevel::ePrimary,
					//(uint32_t)(app->scNumImages*levels) }));
					(uint32_t)(app->scNumImages) }));

		for (int j=0; j<app->scNumImages; j++) {
			vk::raii::CommandBuffer& cmd = mld.cmdBufs[j];

			vk::CommandBufferBeginInfo beginInfo { {}, {} };

			vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
			vk::ClearValue clears_[2] = {
				vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.05f,.1f,1.f } } }, // color
				vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 1.f,1.f,1.f,1.f } } }  // depth
			};

			vk::RenderPassBeginInfo rpInfo {
				*app->simpleRenderPass.pass,
				*app->simpleRenderPass.framebuffers[j],
				aoi,
				{2, clears_}
			};


			cmd.reset();
			cmd.begin(beginInfo);
			cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *mldPipelineStuff.pipeline);
			cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*mldMesh.vertBuffer.buffer}, {0u});
			cmd.bindIndexBuffer(*mldMesh.indBuffer.buffer, {0u}, vk::IndexType::eUint32);

			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *mldPipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *mldPipelineStuff.pipelineLayout, 1, {1,&*mld.descSet}, nullptr);


			//for (uint32_t lvl=0; lvl<levels; lvl++) {
			for (int32_t lvl=cfg.levels-1; lvl>=0; lvl--) {
				//vk::raii::CommandBuffer& cmd = mld.cmdBufs[j*levels+lvl];
				MldPushConstants pushc;
				pushc.lvlOffset = (uint32_t)lvl;
				pushc.numLevels = cfg.levels;
				pushc.expansion = cfg.expansion;
				cmd.pushConstants(*mldPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const MldPushConstants>{1, &pushc});
				/*
				int end   = lvlIndOffset[lvl+1];
				int start = lvlIndOffset[lvl  ];
				*/
				int start,end;
				if (lvl == cfg.levels-1) end = lvlIndOffset[2], start = lvlIndOffset[1];
				else                 end = lvlIndOffset[1], start = lvlIndOffset[0];
				cmd.drawIndexed(end-start, 1, start, 0, 0);
				//cmd.draw(3, 1, 0, 0);
				printf(" - Level %u, inds %d -> %d (%d)\n", lvl, start, end, end-start);
			}


			if(0){
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *app->texturedPipelineStuff.pipeline);
				cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*app->simpleMesh.vertBuffer.buffer}, {0u});
				cmd.bindIndexBuffer(*app->simpleMesh.indBuffer.buffer, {0u}, vk::IndexType::eUint32);

				MeshPushContants pushed;
				for (int i=0; i<16; i++) pushed.model[i] = i % 5 == 0;
				cmd.pushConstants(*app->texturedPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const MeshPushContants>{1, &pushed});

				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *app->texturedPipelineStuff.pipelineLayout, 0, {1,&*app->globalDescSet}, nullptr);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *app->texturedPipelineStuff.pipelineLayout, 1, {1,&*app->texDescSet}, nullptr);

				cmd.drawIndexed(3, 1, 0, 0, 0);
			}


			cmd.endRenderPass();
			cmd.end();
		}
	}

}











