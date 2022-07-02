#include "rt.h"
#include "../gt_impl.hpp"
#include "frastVk/core/fvkShaders.h"

#include "rt_decode.hpp"

#include <fmt/color.h>
#include <fmt/ostream.h>

// Just need to instantiate the templated functions here.
// TODO Have a macro do this in gt_impl.hpp, given for example "RtTypes"

template void GtRenderer<RtTypes, RtTypes::Renderer>::init(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);
// template void GtRenderer<RtTypes, RtTypes::Renderer>::initDebugPipeline(Device& d, TheDescriptorPool& dpool, SimpleRenderPass& pass, Queue& q, Command& cmd, const AppConfig& cfg);

template void GtRenderer<RtTypes, RtTypes::Renderer>::update(GtUpdateContext<RtTypes>&);
template void GtRenderer<RtTypes, RtTypes::Renderer>::render(RenderState&, Command&);
template void GtRenderer<RtTypes, RtTypes::Renderer>::renderDbg(RenderState&, Command&);

template GtPooledData<RtTypes>::GtPooledData();

template void GtDataLoader<RtTypes, RtTypes::DataLoader>::pushAsk(GtAsk<RtTypes>& ask);
template void GtDataLoader<RtTypes, RtTypes::DataLoader>::internalLoop();

RtRenderer::~RtRenderer() {
}

// TODO: Note: I've moved also the 'upload' code in here since it is easier (initially meant to split it up)
int RtDataLoader::loadTile(RtTile* tile, RtTypes::DecodedCpuTileData& dtd, bool isOpen) {
	// Read file from disk
	// Parse protos
	// Unpack to cpu buffer

	// usleep(10'000);
	// return 0;

	std::string fname = renderer.cfg.rootDir + "/node/" + std::string{tile->coord.key, (size_t)tile->coord.level()};
	std::ifstream ifs(fname);


	// fmt::print(" - Decoding {}\n", fname);
	if (rt::decode_node_to_tile(ifs, dtd, false)) {
		fmt::print(fmt::fg(fmt::color::orange), " - [#loadTile] decode '{}' failed, skipping tile.\n", fname);
		// tile->loaded = true;
		// return dtd.meshes.size();
	}

	if (dtd.meshes.size() == 0) {
		// tile->loaded = true; // loaded, but empty
		return 0;
	}

	uint32_t total_meshes = dtd.meshes.size();
	std::vector<uint32_t> gatheredIds(total_meshes);
	renderer.gtpd.withdraw(gatheredIds, !isOpen);
	// renderer.gtpd.withdraw(gatheredIds, false);

	auto &coord = tile->coord;
	int mesh_ii = 0;
	for (auto &meshData : dtd.meshes) {
		uint32_t idx = gatheredIds[mesh_ii++];
		tile->meshIds.push_back(idx);
		// auto error = static_cast<Derived*>(this)->uploadTile(tile, meshData, renderer.gtpd.datas[idx]);
		// auto error = tile->upload(meshData, renderer.gtpd.datas[idx]);
		// assert(not error);
	}

	// fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {}\n", tile->coord.toString());
	// if (dtd.meshes.size() > 1) fmt::print(fmt::fg(fmt::color::dark_gray), " - [#loader] loaded {} ({} meshes)\n", tile->coord.toString(), dtd.meshes.size());
	tile->loaded = true;

	Eigen::Map<Eigen::Matrix4d> mm(dtd.modelMat);
	constexpr double R1         = (6378137.0);
	constexpr double R1i = 1.0 / R1;
	Eigen::Matrix4d scale_m; scale_m <<
		R1i, 0, 0, 0,
		0, R1i, 0, 0,
		0, 0, R1i, 0,
		0, 0, 0, 1;
	mm = scale_m * mm;

	for (int i=0; i<16; i++) tile->model = mm.cast<float>();

	// This needs to be known in the constructor, not here after loading!
	/*
	if (dtd.metersPerTexel <= 0) {
		// TODO: Is this a good approx?
		// dtd.metersPerTexel = 2 * R1 / (1 << tile->nc.level());
		// tile->geoError = 4.0 * (1/255.) / (1 << tile->nc.level());
		// tile->geoError = 8.0 * (1/255.) / (1 << tile->coord.level());
		tile->geoError = (8.0f / 255.f) / (1 << tile->coord.level());
		// tile->geoError = 16.0 * (1/255.) / (1 << tile->nc.level());
		// fmt::print(" - [{}/{}] geoError from lvl {}\n", tile->coord.key, tile->coord.level(), tile->geoError);
	} else {
		// tile->geoError = dtd.metersPerTexel / (255. * R1);
		tile->geoError = dtd.metersPerTexel / (R1);
		// fmt::print(" - [{}/{}] geoError from mpt {}\n", tile->coord.key, tile->coord.level(), tile->geoError);
	}
	*/

	//////////////////////////////////
	// Upload
	//////////////////////////////////

	// fmt::print(fmt::fg(fmt::color::magenta), " - uploading\n");
	tile->uvInfos.resize(tile->meshIds.size()*4);
	for (int i=0; i<tile->meshIds.size(); i++) {
		int meshId = tile->meshIds[i];
		auto &td = renderer.gtpd.datas[meshId];

		// See if we must re-allocate larger buffers.
		// If the texture needs a re-allocation, we must mark that the DescSet needs to be updated too
		auto vert_size = sizeof(PackedVertex)*dtd.meshes[i].vert_buffer_cpu.size();
		auto indx_size = sizeof(uint16_t)*dtd.meshes[i].ind_buffer_cpu.size();
		auto img_size  = sizeof(uint8_t)*dtd.meshes[i].img_buffer_cpu.size();

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
		if (td.tex.extent.width != dtd.meshes[i].texSize[1] or td.tex.extent.height != dtd.meshes[i].texSize[0]) {
			// fmt::print(" - Recreating texture ({} / {}) (wh {} {})\n", img_size, td.tex.capacity_, dtd.meshes[i].texSize[0], dtd.meshes[i].texSize[1]);
			td.mustWriteImageDesc = true;
			td.texOld = std::move(td.tex);
			td.tex.deallocate();
			td.tex.set(VkExtent2D{dtd.meshes[i].texSize[1], dtd.meshes[i].texSize[0]}, VK_FORMAT_R8G8B8A8_UNORM, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			td.tex.create(*uploader.device);
		}

		uploader.enqueueUpload(td.verts, dtd.meshes[i].vert_buffer_cpu.data(), vert_size, 0);
		uploader.enqueueUpload(td.inds, dtd.meshes[i].ind_buffer_cpu.data(), indx_size, 0);
		uploader.enqueueUpload(td.tex, dtd.meshes[i].img_buffer_cpu.data(), img_size, 0, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

		tile->uvInfos[i*4+0] = dtd.meshes[i].uvScale[0];
		tile->uvInfos[i*4+1] = dtd.meshes[i].uvScale[1];
		tile->uvInfos[i*4+2] = dtd.meshes[i].uvOffset[0];
		tile->uvInfos[i*4+3] = dtd.meshes[i].uvOffset[1];

		td.residentInds = dtd.meshes[i].ind_buffer_cpu.size();
		td.residentVerts = dtd.meshes[i].vert_buffer_cpu.size();
	}

	uploader.execute();
	// fmt::print(fmt::fg(fmt::color::magenta), " - done uploading\n");


	return total_meshes;
}

bool RtTile::upload(RtTypes::DecodedCpuTileData::MeshData& dctd, GtTileData& td) {
	// Upload vertices, indices, texture


	return false;
}

// Called for new tiles
void RtTile::updateGlobalBuffer(RtTypes::GlobalBuffer* gpuBuffer) {
	/*struct GlobalBuffer {
		float mvp[16];
		float positionOffset[4];
		float modelMats[16*GT_NUM_TILES];
		float uvScalesAndOffs[4*GT_NUM_TILES];
	}*/

	assert(uvInfos.size()/4 == meshIds.size());
	for (int i=0; i<meshIds.size(); i++) {
		auto index = meshIds[i];
		memcpy(&gpuBuffer->modelMats[16*index], model.data(), 4*16);
		memcpy(&gpuBuffer->uvScalesAndOffs[4*index], &uvInfos[i*4], 4*4);
	}
	uvInfos.clear();
}


void RtRenderer::initPipelinesAndDescriptors(TheDescriptorPool& dpool, SimpleRenderPass& srp, Queue& q, Command& cmd, const AppConfig& cfg) {

		loadShader(*device,
				gfxPipeline.vs,
				gfxPipeline.fs,
				"rt/rt1");

		float viewportXYWH[4] = {
			0,0,
			(float)cfg.width,
			(float)cfg.height
		};
		PipelineBuilder builder;
		builder.depthTest = cfg.depth;

		VertexInputDescription vid;

		std::vector<GraphicsPipeline*> enabledPipelines;
		enabledPipelines.push_back(&gfxPipeline);
		if (this->cfg.allowCaster) enabledPipelines.push_back(&casterStuff.pipeline);


		// uint32_t    location; uint32_t    binding; VkFormat    format; uint32_t    offset;
		VkVertexInputAttributeDescription attrPos  { 0, 0, VK_FORMAT_R8G8B8A8_USCALED, 0 };
		VkVertexInputAttributeDescription attrUv   { 1, 0, VK_FORMAT_R16G16_USCALED,   4 };
		VkVertexInputAttributeDescription attrNrml { 2, 0, VK_FORMAT_R8G8B8A8_USCALED, 8 };
		vid.attributes = { attrPos, attrUv, attrNrml };
		vid.bindings = { VkVertexInputBindingDescription { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX } };

		// Create buffers for each tile

		gfxPipeline.pushConstants.push_back(VkPushConstantRange{
				// VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				VK_SHADER_STAGE_VERTEX_BIT,
				0,
				sizeof(RtPushConstants)});

		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, gfxPipeline.vs, gfxPipeline.fs);

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

		/*
		// Create globalDataBuffer (make it mappable)
		gtpd.globalBuffer.set(sizeof(RtTypes::GlobalBuffer),
					// VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		gtpd.globalBuffer.create(*device);
		*/

		// Create per-tile buffers and fill tile datas
		gtpd.datas.resize(GT_NUM_TILES);
		for (int i=0; i<GT_NUM_TILES; i++) {
			auto &td = gtpd.datas[i];
			td.tex.set(VkExtent2D{256,256}, VK_FORMAT_R8G8B8A8_UNORM, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			td.verts.set(2*2048*sizeof(PackedVertex),
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
		VkDescriptorBufferInfo bufferInfo { gtpd.globalBuffer, 0, sizeof(RtTypes::GlobalBuffer) };
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
						"rt/cast");

			casterStuff.pipeline.pushConstants.push_back(VkPushConstantRange{
					VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RtPushConstants)});

			// uint32_t casterUboBindingIdx = casterStuff.dset.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
			// uint32_t casterImgBindingIdx = casterStuff.dset.addBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
			// casterStuff.dset.create(dpool, {&pipeline});
			fmt::print(" - Caster pipeline has {} descriptor layouts\n", casterStuff.pipeline.setLayouts.size());

			builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, casterStuff.pipeline.vs, casterStuff.pipeline.fs);
			casterStuff.pipeline.create(*device, viewportXYWH, builder, srp, 0);
		}


}
