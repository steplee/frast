#include "frastVk/utils/eigen.h"
#include "rt.h"
#include "frastVk/core/load_shader.hpp"
#include <fmt/ostream.h>
using namespace Eigen;

namespace {
}

namespace rt {

bool RtDataLoader::createBottomLevelAS(RtTile* tile, DecodedTileData& dtd) {
		auto &cmd = myUploader.cmd;
		auto &q = myUploader.q;
		auto &fence = myUploader.fence;
		auto &deviceGpu = app->deviceGpu;

		// fmt::print(" - ModelMat:\n{}\n", Map<const RowMatrix4f>{tile->modelMatf.data()});

		ResidentBuffer& tmpXformBuffer = pooledTileData.tmpXformBuffer;
		if (tmpXformBuffer.residentSize == 0) {
			tmpXformBuffer.setAsStorageBuffer(4*12, true);
			tmpXformBuffer.usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
			tmpXformBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		}
		// TODO: This ought to be persistently mapped.
		VkTransformMatrixKHR* xform = (VkTransformMatrixKHR*) tmpXformBuffer.mem.mapMemory(0, 4*12, {});
		for (int r=0; r<3; r++)
			for (int c=0; c<4; c++)
				// xform->matrix[r][c] = tile->modelMatf[r*4+c];
				// ((float*)xform->matrix)[r*4+c] = tile->modelMatf[r*4+c];
				((float*)xform->matrix)[r*4+c] = r==c;
		vk::DeviceOrHostAddressConstKHR xformAddr;
		xformAddr.deviceAddress = deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*tmpXformBuffer.buffer});

		std::vector<uint32_t> maxPrimCnts;


		// Fill triangle data
		std::vector<vk::AccelerationStructureGeometryKHR> geoms;
		for (int i=0; i<dtd.meshes.size(); i++) {
			auto& mesh = tile->meshes[i];
			auto& td = pooledTileData.datas[mesh.idx];
			auto& md = dtd.meshes[i];
			td.modelMatf = tile->modelMatf;

			vk::DeviceOrHostAddressConstKHR vertsAddr, indsAddr, vertsAddrTmp;
			vertsAddr.deviceAddress = (uint64_t)deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*td.verts.buffer});
			indsAddr.deviceAddress = deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*td.inds.buffer});

			vertsAddrTmp.deviceAddress = (uint64_t)deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*td.vertsFloatRemoveMe.buffer});

			geoms.push_back(vk::AccelerationStructureGeometryKHR{
						vk::GeometryTypeKHR::eTriangles,
						vk::AccelerationStructureGeometryDataKHR {
							vk::AccelerationStructureGeometryTrianglesDataKHR {
								// vk::Format::eR8G8B8Uint, vertsAddr, (VkDeviceSize)sizeof(PackedVertex), td.residentVerts,
								vk::Format::eR32G32B32Sfloat, vertsAddrTmp, (VkDeviceSize)(4*3), td.residentVerts,
								vk::IndexType::eUint16,
								indsAddr,
								{nullptr}
								// xformAddr
							}
						},
						vk::GeometryFlagBitsKHR::eOpaque
			});
			maxPrimCnts.push_back( td.residentInds / 3 );
		}


		vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
		buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
		buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
		buildInfo.pGeometries = geoms.data();
		buildInfo.geometryCount = (uint32_t)geoms.size();


		auto buildSize = deviceGpu.getAccelerationStructureBuildSizesKHR(
				vk::AccelerationStructureBuildTypeKHR::eDevice,
				buildInfo, maxPrimCnts);

		// fmt::print(" - Got acc build size {} {} {}\n", buildSize.accelerationStructureSize, buildSize.updateScratchSize, buildSize.buildScratchSize);

		assert(tile->meshes.size() == 1);
		auto &td0 = pooledTileData.datas[tile->meshes[0].idx];
		if (buildSize.accelerationStructureSize > td0.accel.residentSize) {
			td0.accel.setAsAccelBuffer(buildSize.accelerationStructureSize, false);
			td0.accel.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
			// fmt::print(" - Resizing accel buffer to {}\n", buildSize.accelerationStructureSize);
		}
		if (buildSize.buildScratchSize > myUploader.scratchBuffer.residentSize) {
			myUploader.uploadScratch(nullptr, buildSize.buildScratchSize);
			fmt::print(" - Resizing scratch buffer to {}\n", buildSize.buildScratchSize);
		}
		// if (buildSize.accelerationStructureSize > 0) myUploader.uploadSync(td.accel, md.vert_buffer_cpu.data(), v_size, 0);

		vk::AccelerationStructureCreateInfoKHR createAccelInfo {
			{},
				*td0.accel.buffer,
				0, // offset
				buildSize.accelerationStructureSize,
				vk::AccelerationStructureTypeKHR::eBottomLevel,
				{} // device addr
		};
		td0.accelStructure = std::move(deviceGpu.createAccelerationStructureKHR(createAccelInfo));

		std::vector<vk::AccelerationStructureBuildRangeInfoKHR> rangeInfos_; // Hold heap allocated data
		std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> rangeInfosArr;
		for (int i=0; i<dtd.meshes.size(); i++) {
			auto& md = dtd.meshes[i];
			auto& mesh = tile->meshes[i];
			auto& td = pooledTileData.datas[mesh.idx];
			uint32_t n_prims = td.residentInds / 3;
			rangeInfos_.push_back(vk::AccelerationStructureBuildRangeInfoKHR{
				// uint32_t primitiveCount_  = {}, uint32_t primitiveOffset_ = {}, uint32_t firstVertex_     = {}, uint32_t transformOffset_ = {} 
					// n_vert, 0, 0, 0
					n_prims, 0, 0, 0
			});
		}
		if (geoms.size()) rangeInfosArr.push_back(rangeInfos_.data()); // Act as array to pointers, but we know we only have one outer idx

		buildInfo.dstAccelerationStructure = *td0.accelStructure;

		ResidentBuffer scratchBuffer;
		scratchBuffer.setAsStorageBuffer(buildSize.buildScratchSize, false);
		scratchBuffer.usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
		scratchBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		buildInfo.scratchData.hostAddress = nullptr;
		// buildInfo.scratchData.deviceAddress = deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*myUploader.scratchBuffer.buffer});
		buildInfo.scratchData.deviceAddress = deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*scratchBuffer.buffer});

		cmd.reset();
		cmd.begin(vk::CommandBufferBeginInfo{});
		cmd.buildAccelerationStructuresKHR({1u, &buildInfo}, {1u, rangeInfosArr.data()});
		cmd.end();
		vk::SubmitInfo submitInfo {
			{},
			{},
				{1u, &*cmd},
				{}
		};
		q.submit(submitInfo, *fence);
		deviceGpu.waitForFences({1u, &*fence}, true, 999999999999);
		deviceGpu.resetFences({1u, &*fence});
		// fmt::print(" - Done creating BLAS\n");

		td0.accelAddress = deviceGpu.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR { *td0.accelStructure });
		tmpXformBuffer.mem.unmapMemory();

		return false;
}


bool RtRenderer::createThenSwapTopLevelAS(vk::raii::CommandBuffer& cmd, vk::raii::Queue& q, vk::raii::Fence& fence) {
	auto& ptd = pooledTileData;

	VkTransformMatrixKHR xform1 = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f };

	// A mask indicating which tiles are in use.
	std::vector<bool> residentIds(cfg.maxTiles);
	int n = ptd.tellInUse(residentIds);

	if (n <= 0) {
		fmt::print(" - No resident tiles, skipping.\n");
		return true;
	}
	fmt::print(" - Building TLAS with {} tiles.\n", n);

	std::vector<uint32_t> primCnts(n);
	uint32_t totalPrims = 0;
	std::vector<vk::AccelerationStructureInstanceKHR> insts(n);

	for (int i=0, j=0; i<cfg.maxTiles; i++) {
		if (not residentIds[i]) continue;

		VkTransformMatrixKHR xform2;
		if (ptd.datas[i].modelMatf.size()) {
			for (int r=0; r<3; r++)
				for (int c=0; c<4; c++)
					xform2.matrix[r][c] = ptd.datas[i].modelMatf[r*4+c];
		} else {
			xform2 = xform1;
			fmt::print(" - tile id {} missing modelMatf\n", i);
		}

		primCnts[j] = ptd.datas[i].residentInds/3;
		totalPrims += primCnts[j];
		insts[j++] = vk::AccelerationStructureInstanceKHR {
			xform1,
			// xform2,
			(uint32_t)i,
			0xff,
			0,
			vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable,
			ptd.datas[i].accelAddress
		};
		// fmt::print(" - Tile {} is resident with {} inds\n", i, primCnts[j-1]);
	}
	
	ResidentBuffer buf;
	buf.setAsStorageBuffer(sizeof(vk::AccelerationStructureInstanceKHR)*insts.size(), true);
	buf.usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	buf.memPropFlags |= vk::MemoryPropertyFlagBits::eHostCoherent;
	buf.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
	buf.upload(insts.data(), sizeof(vk::AccelerationStructureInstanceKHR)*insts.size());

	VkDeviceOrHostAddressConstKHR instDataAddr{};
	instDataAddr.deviceAddress = (uint64_t)app->deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*buf.buffer});
	fmt::print(" - Uploaded {} insts to addr {}.\n", insts.size(), instDataAddr.deviceAddress);

	std::vector<vk::AccelerationStructureGeometryKHR> geoms;
	geoms.push_back(vk::AccelerationStructureGeometryKHR{
				vk::GeometryTypeKHR::eInstances,
				vk::AccelerationStructureGeometryDataKHR {
					vk::AccelerationStructureGeometryInstancesDataKHR {
						false,
						(uint64_t)instDataAddr.deviceAddress
					}
				},
				vk::GeometryFlagBitsKHR::eOpaque
	});

	vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
	buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
	buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
	buildInfo.pGeometries = geoms.data();
	buildInfo.geometryCount = (uint32_t)geoms.size();

	auto buildSize = app->deviceGpu.getAccelerationStructureBuildSizesKHR(
			vk::AccelerationStructureBuildTypeKHR::eDevice,
			// buildInfo, primCnts);
			buildInfo, {(uint32_t)n});

	if (buildSize.accelerationStructureSize > nextTlasBuf.residentSize) {
		nextTlasBuf.setAsAccelBuffer(buildSize.accelerationStructureSize, false);
		nextTlasBuf.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		fmt::print(" - Resizing TLAS buffer to {}\n", buildSize.accelerationStructureSize);
	}

	vk::AccelerationStructureCreateInfoKHR createInfo {
			{},
			*nextTlasBuf.buffer,
			0,
			buildSize.accelerationStructureSize,
			vk::AccelerationStructureTypeKHR::eTopLevel,
			{}
	};
	nextTlas = std::move(app->deviceGpu.createAccelerationStructureKHR(createInfo));

	ResidentBuffer scratchBuffer;
	scratchBuffer.setAsStorageBuffer(buildSize.buildScratchSize, false);
	scratchBuffer.usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
	scratchBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);

	buildInfo.scratchData.deviceAddress = app->deviceGpu.getBufferAddress(vk::BufferDeviceAddressInfo{*scratchBuffer.buffer});
	buildInfo.dstAccelerationStructure = *nextTlas;
	fmt::print(" - Scratch addr {}\n", buildInfo.scratchData.deviceAddress);

	std::vector<vk::AccelerationStructureBuildRangeInfoKHR> rangeInfos_; // Hold heap allocated data
	std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> rangeInfosArr;
	rangeInfos_.push_back(vk::AccelerationStructureBuildRangeInfoKHR{ (uint32_t)n, 0, 0, 0 });
	if (geoms.size()) rangeInfosArr.push_back(rangeInfos_.data()); // Act as array to pointers, but we know we only have one outer idx

	cmd.reset();
	cmd.begin(vk::CommandBufferBeginInfo{});
	cmd.buildAccelerationStructuresKHR({1u, &buildInfo}, {1u, rangeInfosArr.data()});
	cmd.end();
	vk::SubmitInfo submitInfo {
		{},
			{},
			{1u, &*cmd},
			{}
	};
	q.submit(submitInfo, *fence);
	app->deviceGpu.waitForFences({1u, &*fence}, true, 999999999999);
	app->deviceGpu.resetFences({1u, &*fence});
	// fmt::print(" - Done creating BLAS\n");

	nextTlasAddr = app->deviceGpu.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR { *nextTlas });

	currTlas = std::move(nextTlas);
	currTlasAddr = std::move(nextTlasAddr);
	currTlasBuf = std::move(nextTlasBuf);
	nextTlasBuf.residentSize = 0;

	// TODO: Have TWO sets and just swap them... but would that work since tlas is recreated (but uses same buffer?)
	writeDescSetTlas();

	return false;
}


bool RtRenderer::setupRaytracePipelines() {

	vk::CommandPoolCreateInfo poolInfo { vk::CommandPoolCreateFlagBits::eResetCommandBuffer, app->queueFamilyGfxIdxs[0] };
	cmdPool = std::move(app->deviceGpu.createCommandPool(poolInfo));
	vk::CommandBufferAllocateInfo bufInfo { *cmdPool, vk::CommandBufferLevel::ePrimary, 2 };
	cmdBuffers = std::move(app->deviceGpu.allocateCommandBuffers(bufInfo));
	fences.push_back(std::move(app->deviceGpu.createFence({})));

	loadOneShader(app->deviceGpu, raytracePipelineStuff.gen, "rtRaytrace/1.rg.glsl");
	loadOneShader(app->deviceGpu, raytracePipelineStuff.closestHit, "rtRaytrace/1.rch.glsl");
	loadOneShader(app->deviceGpu, raytracePipelineStuff.miss, "rtRaytrace/1.rm.glsl");


	// Descriptor stuff
	setupRaytraceDescriptors();

	raytracePipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
	raytracePipelineStuff.build(app);

	{
		vk::DescriptorBufferInfo binfo_2 { *raytraceCameraBuffer.buffer, 0, VK_WHOLE_SIZE };
		// vk::DescriptorImageInfo iinfo_1 { *raytracePipelineStuff.storageImage.sampler, *raytracePipelineStuff.storageImage.view, vk::ImageLayout::eShaderReadOnlyOptimal };
		vk::DescriptorImageInfo iinfo_1 { *raytracePipelineStuff.storageImage.sampler, *raytracePipelineStuff.storageImage.view, vk::ImageLayout::eGeneral };
		std::vector<vk::WriteDescriptorSet> writeDesc {
			// writeAcc,
			{
				*raytraceDescSet,
				1, 0, 1,
				vk::DescriptorType::eStorageImage,
				&iinfo_1, nullptr, nullptr },
			{
				*raytraceDescSet,
				2, 0, 1,
				vk::DescriptorType::eUniformBuffer,
				nullptr, &binfo_2, nullptr }
		};
		app->deviceGpu.updateDescriptorSets({writeDesc}, nullptr);
	}

	{
		auto &q = app->queueGfx;
		auto &cmd = cmdBuffers[0];
		auto &fence = fences[0];

		std::vector<vk::ImageMemoryBarrier> barriers;
		vk::ImageMemoryBarrier barrier;
		barrier.image = *raytracePipelineStuff.storageImage.image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eUndefined;
		barrier.newLayout = vk::ImageLayout::eGeneral;
		barriers.push_back(barrier);

		cmd.reset();
		cmd.begin(vk::CommandBufferBeginInfo{});
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {barriers});
		cmd.end();
		vk::SubmitInfo submitInfo { {}, {}, {1u, &*cmd}, {} };
		q.submit(submitInfo, *fence);
		app->deviceGpu.waitForFences({1u, &*fence}, true, 999999999999);
		app->deviceGpu.resetFences({1u, &*fence});
	}

	{
		std::vector<uint8_t> img(app->windowHeight*app->windowWidth*4*3);
		for (int i=0; i<app->windowHeight*app->windowWidth*4*3; i++) img[i] = i % 255;
		Uploader myUploader(app, *app->queueGfx);
		myUploader.uploadSync(raytracePipelineStuff.storageImage, img.data(), img.size(), 0);
	}


	fmt::print(" - [RtRenderer] Done constructing raytracing pipeline.\n");
	return false;
}

bool RtRenderer::setupRaytraceDescriptors() {

	// We will re-use pooledTileData.descSet, which holds the texture array for the tiles
	{
	}

	raytraceCameraBuffer.setAsUniformBuffer(sizeof(RtRaytraceCameraData), true);
	raytraceCameraBuffer.memPropFlags |= vk::MemoryPropertyFlagBits::eHostCoherent;
	raytraceCameraBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);

	// Setup descriptor for raygen shader
	{
		std::vector<vk::DescriptorSetLayoutBinding> bindings = {
			{ 0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR },
			{ 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR },
			{ 2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR } };

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
		raytraceDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *descPool, 1, &*raytraceDescSetLayout };
		raytraceDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		/*
		vk::WriteDescriptorSetAccelerationStructureKHR writeAcc_;
		writeAcc_.accelerationStructureCount = 1;
		writeAcc_.pAccelerationStructures = &*currTlas;
		vk::WriteDescriptorSet writeAcc {
				*raytraceDescSet,
				0, 0, 1,
				vk::DescriptorType::eAccelerationStructureKHR,
				nullptr, nullptr, nullptr };
		writeAcc.pNext = (void*)&writeAcc_;
		*/

	}

	raytracePipelineStuff.setLayouts.push_back(*raytraceDescSetLayout);
	raytracePipelineStuff.setLayouts.push_back(*pooledTileData.descSetLayout);

	return false;
}

void RtRenderer::writeDescSetTlas() {
	fmt::print(" - Writing TLAS descriptor\n");

	vk::WriteDescriptorSetAccelerationStructureKHR writeAcc_;
	writeAcc_.accelerationStructureCount = 1;
	writeAcc_.pAccelerationStructures = &*currTlas;
	vk::WriteDescriptorSet writeAcc {
			*raytraceDescSet,
			0, 0, 1,
			vk::DescriptorType::eAccelerationStructureKHR,
			nullptr, nullptr, nullptr };
	writeAcc.pNext = (void*)&writeAcc_;

	std::vector<vk::WriteDescriptorSet> writeDesc { writeAcc };

	app->deviceGpu.updateDescriptorSets({writeDesc}, nullptr);
	tlasIsSet = true;
}

}
