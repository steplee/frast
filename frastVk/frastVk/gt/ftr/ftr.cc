#include "ftr.h"
#include "../gt_impl.hpp"
#include "frastVk/core/fvkShaders.h"

#include "conversions.hpp"

#include <fmt/color.h>
#include <fmt/ostream.h>

// Just need to instantiate the templated functions here.
// TODO Have a macro do this in gt_impl.hpp, given for example "FtTypes"

template void GtRenderer<FtTypes, FtTypes::Renderer>::init(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);
// template void GtRenderer<FtTypes, FtTypes::Renderer>::initDebugPipeline(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);

template void GtRenderer<FtTypes, FtTypes::Renderer>::update(GtUpdateContext<FtTypes>&);
template void GtRenderer<FtTypes, FtTypes::Renderer>::render(RenderState&, Command&);
template void GtRenderer<FtTypes, FtTypes::Renderer>::renderDbg(RenderState&, Command&);

template GtPooledData<FtTypes>::GtPooledData();

template void GtDataLoader<FtTypes, FtTypes::DataLoader>::pushAsk(GtAsk<FtTypes>& ask);
template void GtDataLoader<FtTypes, FtTypes::DataLoader>::internalLoop();

static uint32_t log2_(uint32_t x) {
	uint32_t y = 0;
	while (x>>=1) y++;
	return y;
}

FtDataLoader::FtDataLoader(typename FtTypes::Renderer& renderer_) : GtDataLoader<FtTypes, FtDataLoader>(renderer_) {
	DatasetReaderOptions dopts1, dopts2;
	dopts1.allowInflate = false;
	dopts2.allowInflate = true;
	colorDset = new DatasetReader(renderer.cfg.colorDsetPath, dopts1);
	if (renderer.cfg.elevDsetPath.length())
		elevDset  = new DatasetReader(renderer.cfg.elevDsetPath,  dopts2);

	colorDset->beginTxn(&color_txn, true);
	if (elevDset)
		elevDset->beginTxn(&elev_txn, true);

	assert(renderer.cfg.texSize == colorDset->tileSize());

	renderer.colorFormat = Image::Format::RGBA;
	// if (colorDset->format() == Image::Format::GRAY or cfg.channels == 1) renderer.colorFormat = Image::Format::GRAY;
	colorBuf = Image { (int)colorDset->tileSize(), (int)colorDset->tileSize(), renderer.colorFormat };
	colorBuf.alloc();
	if (renderer.colorFormat == Image::Format::RGBA)
		for (int y=0; y<renderer.cfg.texSize; y++)
		for (int x=0; x<renderer.cfg.texSize; x++)
			colorBuf.buffer[y*renderer.cfg.texSize*4+x*4+3] = 255;

	if (elevDset) {
		elevBuf = Image { (int)elevDset->tileSize(), (int)elevDset->tileSize(), Image::Format::TERRAIN_2x8 };
		elevBuf.alloc();
		memset(elevBuf.buffer, 0, elevBuf.size());
	}

}

void FtDataLoader::loadColor(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh) {
	auto& tex = renderer.gtpd.datas[tile->meshIds[0]].tex;

	//int DatasetReader::fetchBlocks(Image& out, uint64_t lvl, const uint64_t tlbr[4], MDB_txn* txn0) {
	uint64_t tlbr[4] = { tile->coord.x(), tile->coord.y(), tile->coord.x()+1lu, tile->coord.y()+1lu };
	int n_missing = colorDset->fetchBlocks(colorBuf, tile->coord.z(), tlbr, color_txn);
	// fmt::print(" - [#loadColor()] fetched tiles from tlbr {} {} {} {} ({} missing)\n", tlbr[0],tlbr[1],tlbr[2],tlbr[3], n_missing);
	// if (n_missing > 0) return true;
	if (n_missing > 0) return;

	mesh.img_buffer_cpu.resize(colorBuf.size());
	memcpy(mesh.img_buffer_cpu.data(), colorBuf.buffer, colorBuf.size());
}

void FtDataLoader::loadElevAndMetaWithDted(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh, const FtTypes::Config& cfg) {
	uint64_t tz = tile->coord.z();
	uint64_t ty = tile->coord.y();
	uint64_t tx = tile->coord.x();
	float lvlScale = 2. / (1 << tz);
	float ox = tx * lvlScale * 1. - 1.;
	float oy = (ty * lvlScale * 1. - 1.);

	/*
	double bboxWm[4] = {
			ox * WebMercatorMapScale,
			oy * WebMercatorMapScale,
			(ox+lvlScale) * WebMercatorMapScale,
			(oy+lvlScale) * WebMercatorMapScale,
	};
	bool failure = elevDset->rasterIo(elevBuf, bboxWm);
	*/

	uint16_t res_ratio = cfg.texSize / cfg.vertsAlongEdge; // 32 for now
	uint32_t lvlOffset = log2_(res_ratio);
	uint64_t z = tz - lvlOffset;
	uint64_t y = ty >> lvlOffset;
	uint64_t x = tx >> lvlOffset;
	uint64_t tlbr[4] = { x,y, x+1lu,y+1lu };
	int n_missing = elevDset->fetchBlocks(elevBuf, z, tlbr, elev_txn);

	if (n_missing > 0) {
		memset(elevBuf.buffer, 0, elevBuf.size());
	}

	int S = cfg.vertsAlongEdge;
	for (uint16_t yy=0; yy<cfg.vertsAlongEdge-1; yy++)
		for (uint16_t xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
			uint16_t a = ((yy+0)*S) + (xx+0);
			uint16_t b = ((yy+0)*S) + (xx+1);
			uint16_t c = ((yy+1)*S) + (xx+1);
			uint16_t d = ((yy+1)*S) + (xx+0);
			mesh.ind_buffer_cpu.push_back(a); mesh.ind_buffer_cpu.push_back(b); mesh.ind_buffer_cpu.push_back(c);
			mesh.ind_buffer_cpu.push_back(c); mesh.ind_buffer_cpu.push_back(d); mesh.ind_buffer_cpu.push_back(a);
		}

	mesh.vert_buffer_cpu.resize(cfg.vertsAlongEdge*cfg.vertsAlongEdge);
	uint32_t y_off = (res_ratio - 1 - (ty % res_ratio)) * cfg.texSize / res_ratio;
	uint32_t x_off = (tx % res_ratio) * cfg.texSize / res_ratio;
	uint16_t* elevData = (uint16_t*) elevBuf.buffer;

	for (int32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
			int32_t ii = ((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(cfg.vertsAlongEdge-1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(cfg.vertsAlongEdge-1);
			float xxx = (xxx_ * lvlScale) + ox;
			float yyy = ((1. - yyy_) * lvlScale) + oy;
			// float zzz = (((uint16_t*)elevBuf.buffer)[yy*cfg.vertsAlongEdge+xx]) / (8.0f * static_cast<float>(WebMercatorMapScale));
			// float zzz = (((uint16_t*)elevBuf.buffer)[(cfg.vertsAlongEdge-yy-1)*cfg.vertsAlongEdge+xx]) / (8.0f * static_cast<float>(WebMercatorMapScale));
			// float zzz = (elevData[(cfg.vertsAlongEdge-yy-1)*cfg.vertsAlongEdge+xx]) / (4.0f * static_cast<float>(WebMercatorMapScale));
			float zzz = (elevData[(yy+y_off)*elevBuf.w+xx+x_off] / 8.0) / WebMercatorMapScale;
			mesh.vert_buffer_cpu[ii].x = xxx;
			mesh.vert_buffer_cpu[ii].y = yyy;
			mesh.vert_buffer_cpu[ii].z = zzz;
			mesh.vert_buffer_cpu[ii].w = 0;
			mesh.vert_buffer_cpu[ii].u = xxx_;
			mesh.vert_buffer_cpu[ii].v = yyy_;

			// fmt::print(" - vert {} {} {} | {} {}\n", xxx,yyy,zzz, xxx_,yyy_);
		}

	unit_wm_to_ecef((float*)mesh.vert_buffer_cpu.data(), mesh.vert_buffer_cpu.size(), (float*)mesh.vert_buffer_cpu.data(), 6);
}

namespace {
void loadElevAndMetaNoDted(FtTile* tile, FtTypes::DecodedCpuTileData::MeshData& mesh, const FtTypes::Config& cfg) {
	int64_t tz = tile->coord.z();
	int64_t ty = tile->coord.y();
	int64_t tx = tile->coord.x();
	float lvlScale = 2. / (1 << tz);
	float ox = tx * lvlScale * 1. - 1.;
	float oy = (ty * lvlScale * 1. - 1.);

	int S = cfg.vertsAlongEdge;
	for (uint16_t yy=0; yy<cfg.vertsAlongEdge-1; yy++)
		for (uint16_t xx=0; xx<cfg.vertsAlongEdge-1; xx++) {
			uint16_t a = ((yy+0)*S) + (xx+0);
			uint16_t b = ((yy+0)*S) + (xx+1);
			uint16_t c = ((yy+1)*S) + (xx+1);
			uint16_t d = ((yy+1)*S) + (xx+0);
			mesh.ind_buffer_cpu.push_back(a); mesh.ind_buffer_cpu.push_back(b); mesh.ind_buffer_cpu.push_back(c);
			mesh.ind_buffer_cpu.push_back(c); mesh.ind_buffer_cpu.push_back(d); mesh.ind_buffer_cpu.push_back(a);
		}

	mesh.vert_buffer_cpu.resize(cfg.vertsAlongEdge*cfg.vertsAlongEdge);

	for (int32_t yy=0; yy<cfg.vertsAlongEdge; yy++)
		for (int32_t xx=0; xx<cfg.vertsAlongEdge; xx++) {
			int32_t ii = ((cfg.vertsAlongEdge-1-yy)*cfg.vertsAlongEdge)+xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(cfg.vertsAlongEdge-1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(cfg.vertsAlongEdge-1);
			float xxx = (xxx_ * lvlScale) + ox;
			float yyy = ((1. - yyy_) * lvlScale) + oy;
			mesh.vert_buffer_cpu[ii].x = xxx;
			mesh.vert_buffer_cpu[ii].y = yyy;
			mesh.vert_buffer_cpu[ii].z = 0;
			mesh.vert_buffer_cpu[ii].w = 0;
			mesh.vert_buffer_cpu[ii].u = xxx_;
			mesh.vert_buffer_cpu[ii].v = yyy_;

			// fmt::print(" - vert {} {} {} | {} {}\n", xxx,yyy,0, xxx_,yyy_);
		}

	unit_wm_to_ecef((float*)mesh.vert_buffer_cpu.data(), mesh.vert_buffer_cpu.size(), (float*)mesh.vert_buffer_cpu.data(), 6);
}
}


FtRenderer::~FtRenderer() {
}

// TODO: Note: I've moved also the 'upload' code in here since it is easier (initially meant to split it up)
int FtDataLoader::loadTile(FtTile* tile, FtTypes::DecodedCpuTileData& dtd, bool isOpen) {

	loadColor(tile, dtd.mesh);
	if (elevDset)
		loadElevAndMetaWithDted(tile, dtd.mesh, renderer.cfg);
	else
		loadElevAndMetaNoDted(tile, dtd.mesh, renderer.cfg);

	uint32_t total_meshes = 1;
	std::vector<uint32_t> gatheredIds(total_meshes);
	renderer.gtpd.withdraw(gatheredIds, !isOpen);
	// renderer.gtpd.withdraw(gatheredIds, false);

	auto &coord = tile->coord;
	int mesh_ii = 0;
	tile->meshIds.push_back(gatheredIds[0]);

	// TODO: Is this a good approx?
	// tile->geoError = (4.0f / 255.f) / (1 << tile->coord.level());

	//////////////////////////////////
	// Upload
	//////////////////////////////////

	// fmt::print(fmt::fg(fmt::color::magenta), " - uploading\n");
	{
		int meshId = tile->meshIds[0];
		auto &td = renderer.gtpd.datas[meshId];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		// NOTE: This will not happen in ftr (but does in rt)
		auto vert_size = sizeof(FtPackedVertex)*dtd.mesh.vert_buffer_cpu.size();
		auto indx_size = sizeof(uint16_t)*dtd.mesh.ind_buffer_cpu.size();
		auto img_size  = sizeof(uint8_t)*dtd.mesh.img_buffer_cpu.size();

		if (vert_size > td.verts.capacity_) {
			td.verts.deallocate();
			td.verts.set(vert_size * 2,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			td.verts.create(*uploader.device);
		}
		if (indx_size > td.inds.capacity_) {
			td.inds.deallocate();
			td.inds.set(indx_size * 2,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
			td.inds.create(*uploader.device);
		}
		// TODO: Plenty of optimizations to do here
		// if (img_size > td.tex.capacity_) {
		/*if (td.tex.extent.width != dtd.mesh.texSize[1] or td.tex.extent.height != dtd.mesh.texSize[0]) {
			// fmt::print(" - Recreating texture ({} / {}) (wh {} {})\n", img_size, td.tex.capacity_, dtd.mesh.texSize[0], dtd.mesh.texSize[1]);
			td.mustWriteImageDesc = true;
			td.texOld = std::move(td.tex);
			td.tex.deallocate();
			td.tex.set(VkExtent2D{dtd.mesh.texSize[1], dtd.mesh.texSize[0]}, VK_FORMAT_R8G8B8A8_UNORM, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			td.tex.create(*uploader.device);
		}*/

		uploader.enqueueUpload(td.verts, dtd.mesh.vert_buffer_cpu.data(), vert_size, 0);
		uploader.enqueueUpload(td.inds, dtd.mesh.ind_buffer_cpu.data(), indx_size, 0);
		uploader.enqueueUpload(td.tex, dtd.mesh.img_buffer_cpu.data(), img_size, 0, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		td.residentInds = dtd.mesh.ind_buffer_cpu.size();
		td.residentVerts = dtd.mesh.vert_buffer_cpu.size();
	}

	uploader.execute();

	// fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {} ({} meshes)\n", tile->coord.toString(), total_meshes);
	tile->loaded = true;

	return total_meshes;
}

bool FtTile::upload(FtTypes::DecodedCpuTileData::MeshData& dctd, GtTileData& td) {
	// Upload vertices, indices, texture
	return false;
}

// Called for new tiles
void FtTile::updateGlobalBuffer(FtTypes::GlobalBuffer* gpuBuffer) {
}


void FtRenderer::initPipelinesAndDescriptors(TheDescriptorPool& dpool, SimpleRenderPass& srp, Queue& q, Command& cmd, const AppConfig& cfg) {
	
		std::vector<GraphicsPipeline*> enabledPipelines;
		enabledPipelines.push_back(&gfxPipeline);
		if (this->cfg.allowCaster) enabledPipelines.push_back(&casterStuff.pipeline);

		assert(not loadShader(*device,
				gfxPipeline.vs,
				gfxPipeline.fs,
				"ftr/ftr1"));

		float viewportXYWH[4] = {
			0,0,
			(float)cfg.width,
			(float)cfg.height
		};
		PipelineBuilder builder;
		builder.depthTest = cfg.depth;

		VertexInputDescription vid;

		// uint32_t    location; uint32_t    binding; VkFormat    format; uint32_t    offset;
		VkVertexInputAttributeDescription attrPos  { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 };
		VkVertexInputAttributeDescription attrUv   { 1, 0, VK_FORMAT_R32G32_SFLOAT,   4*4 };
		// VkVertexInputAttributeDescription attrNrml { 2, 0, VK_FORMAT_R8G8B8A8_USCALED, 8 };
		vid.attributes = { attrPos, attrUv /*, attrNrml*/ };
		vid.bindings = { VkVertexInputBindingDescription { 0, sizeof(FtPackedVertex), VK_VERTEX_INPUT_RATE_VERTEX } };

		// Create buffers for each tile

		if (FtTypes::PushConstants::Enabled)
			gfxPipeline.pushConstants.push_back(VkPushConstantRange{
					VK_SHADER_STAGE_VERTEX_BIT,
					0,
					sizeof(FtPushConstants)});

		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, gfxPipeline.vs, gfxPipeline.fs);

		// Create descriptors
		// There are two (or three) descriptor sets
		//    Set0: GlobalData
		//          binding0: UBO holding MVP as well as an array of model matrices etc. for each tile
		//    Set1: TileData
		//          binding0: Array of image textures
		//    Set2: CasterData
		//          (TODO)

		// GlobalData
		uint32_t globalDataBindingIdx = gtpd.globalDescSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		gtpd.globalDescSet.create(dpool, enabledPipelines);

		// TileData
		uint32_t tileImgsBindingIdx = gtpd.tileDescSet.addBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GT_NUM_TILES, VK_SHADER_STAGE_FRAGMENT_BIT);
		gtpd.tileDescSet.create(dpool, enabledPipelines);

		// mat44_map = (float*) globalDataBuffer.map();
		// for (int i=0; i<16; i++) mat44_map[i] = (i%5) == 0;

		// Create pipeline

		gfxPipeline.create(*device, viewportXYWH, builder, srp, 0);

		// Create per-tile buffers and fill tile datas
		gtpd.datas.resize(GT_NUM_TILES);
		for (int i=0; i<GT_NUM_TILES; i++) {
			auto &td = gtpd.datas[i];
			td.tex.set(VkExtent2D{256,256}, VK_FORMAT_R8G8B8A8_UNORM, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			td.verts.set(2*2048*sizeof(FtPackedVertex),
					// VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
			td.inds.set(2*2048*sizeof(uint16_t),
					// VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
			td.tex.create(*device);
			td.verts.create(*device);
			td.inds.create(*device);
		}

		// Write with buffers

		// GlobalData
		VkDescriptorBufferInfo bufferInfo { gtpd.globalBuffer, 0, sizeof(FtTypes::GlobalBuffer) };
		gtpd.globalDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				gtpd.globalDescSet, 0,
				0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				nullptr, &bufferInfo, nullptr
				});

		// TileData
		std::vector<VkDescriptorImageInfo> imgInfos(GT_NUM_TILES);
		for (int i=0; i<GT_NUM_TILES; i++) imgInfos[i] = VkDescriptorImageInfo { sampler, gtpd.datas[i].tex.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
		gtpd.tileDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				gtpd.tileDescSet, 0,
				0, GT_NUM_TILES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				imgInfos.data(), nullptr, nullptr
				});


		// Transition images to correct format

		Barriers barriers;
		for (int i=0; i<GT_NUM_TILES; i++) {
			barriers.append(gtpd.datas[i].tex, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		}
		cmd.begin().barriers(barriers).end();
		Fence fence { *device };
		Submission submission { { *device, q } };
		submission.fence = fence;
		submission.submit(&cmd.cmdBuf, 1, true);



		// Setup caster stuff

		if (this->cfg.allowCaster) {
			do_init_caster_stuff(*device, 2, dpool);

			PipelineBuilder builder;
			builder.depthTest = cfg.depth;
			loadShader(*device,
						casterStuff.pipeline.vs,
						casterStuff.pipeline.fs,
						"ftr/cast");


			if (FtPushConstants::Enabled)
				casterStuff.pipeline.pushConstants.push_back(VkPushConstantRange{
						VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FtPushConstants)});

			// fmt::print(" - Caster pipeline has {} descriptor layouts\n", casterStuff.pipeline.setLayouts.size());

			builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, casterStuff.pipeline.vs, casterStuff.pipeline.fs);
			casterStuff.pipeline.create(*device, viewportXYWH, builder, srp, 0);
		}



}
