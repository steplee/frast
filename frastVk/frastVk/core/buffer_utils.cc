#include "buffer_utils.h"
#include "app.h"

#include <fmt/core.h>



uint64_t scalarSizeOfFormat(const vk::Format& f) {
	switch (f) {
		case vk::Format::eD32Sfloat: return 4*1;
		case vk::Format::eR32Sfloat: return 4*1;
		case vk::Format::eR32G32Sfloat: return 4*2;
		case vk::Format::eR32G32B32Sfloat: return 4*3;
		case vk::Format::eR32G32B32A32Sfloat: return 4*4;

		case vk::Format::eR64Sfloat: return 8*1;
		case vk::Format::eR64G64Sfloat: return 8*2;
		case vk::Format::eR64G64B64Sfloat: return 8*3;
		case vk::Format::eR64G64B64A64Sfloat: return 8*4;

		case vk::Format::eR8Uint:
		case vk::Format::eR8Sint:
		case vk::Format::eR8Unorm:
		case vk::Format::eR8Snorm:
		case vk::Format::eR8Sscaled:
		case vk::Format::eR8Uscaled:
											  return 1;
		case vk::Format::eR8G8Uint:
		case vk::Format::eR8G8Sint:
		case vk::Format::eR8G8Unorm:
		case vk::Format::eR8G8Snorm:
		case vk::Format::eR8G8Sscaled:
		case vk::Format::eR8G8Uscaled:
											  return 2;
		case vk::Format::eR8G8B8Uint:
		case vk::Format::eR8G8B8Sint:
		case vk::Format::eR8G8B8Unorm:
		case vk::Format::eR8G8B8Snorm:
		case vk::Format::eR8G8B8Sscaled:
		case vk::Format::eR8G8B8Uscaled:
											  return 3;
		case vk::Format::eR8G8B8A8Uint:
		case vk::Format::eR8G8B8A8Sint:
		case vk::Format::eR8G8B8A8Unorm:
		case vk::Format::eR8G8B8A8Snorm:
		case vk::Format::eR8G8B8A8Sscaled:
		case vk::Format::eR8G8B8A8Uscaled:
		case vk::Format::eB8G8R8A8Uint:
		case vk::Format::eB8G8R8A8Sint:
		case vk::Format::eB8G8R8A8Unorm:
		case vk::Format::eB8G8R8A8Snorm:
		case vk::Format::eB8G8R8A8Sscaled:
		case vk::Format::eB8G8R8A8Uscaled:
											  return 4;
		default: {
					 throw std::runtime_error("[scalarSizeOfFormat] Unsupported format " + vk::to_string(f));
				 }
	}
	return 0;
}

/* ===================================================
 *
 *
 *                  ExBuffer
 *
 *
 * =================================================== */


void* ExBuffer::map() {
	assert(mappedAddr == nullptr);
	mappedAddr = (void*) mem.mapMemory(0, residentSize, {});
	return mappedAddr;
}
void ExBuffer::unmap() {
	assert(mappedAddr);
	mem.unmapMemory();
}

//void ResidentBuffer::create(vk::raii::Device& d, const vk::PhysicalDevice& pd, const std::vector<uint32_t>& queueFamilyIndices) {
void ExBuffer::create(QueueDeviceSpec& spec) {
	assert(givenSize>0);

	uint32_t idx = 0;


	vk::BufferCreateInfo binfo {
		{}, givenSize,
		usageFlags,
		sharingMode, spec.queueFamilyIndices
	};
	buffer = std::move(vk::raii::Buffer{spec.dev.createBuffer(binfo)});

	auto req = buffer.getMemoryRequirements();
	residentSize = req.size;
	//printf(" - allocating buffer to memory type idx %u, givenSize %lu, residentSize %lu\n", idx, givenSize, residentSize);

	uint32_t memMask = req.memoryTypeBits;
	idx = findMemoryTypeIndex(*spec.pdev, memPropFlags, memMask);

	vk::MemoryAllocateInfo allocInfo { residentSize, idx };

	vk::MemoryAllocateFlagsInfo memAllocInfo { vk::MemoryAllocateFlagBits::eDeviceAddress };
	if (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
		allocInfo.pNext = &memAllocInfo;
	}

	mem = std::move(vk::raii::DeviceMemory(spec.dev, allocInfo));

	buffer.bindMemory(*mem, 0);
}

void ExBuffer::uploadNow(void* cpuData, uint64_t len, uint64_t offset) {
	assert(len <= residentSize);
	if (not mappedAddr) {
		void* dbuf = (void*) mem.mapMemory(0, residentSize, {});
		memcpy(dbuf, cpuData, len);
		mem.unmapMemory();
	} else {
		memcpy(mappedAddr, cpuData, len);
	}
}

void ExBuffer::setAsBuffer(uint64_t len, bool mappable, vk::Flags<vk::BufferUsageFlagBits> usage) {
	givenSize = len;
	memPropFlags = mappable ? vk::MemoryPropertyFlagBits::eHostVisible
		                    : vk::MemoryPropertyFlagBits::eDeviceLocal;
	usageFlags = usage;
	if (not mappable) usageFlags |= vk::BufferUsageFlagBits::eTransferDst;
}

bool ExBuffer::copyFromImage(const vk::CommandBuffer &copyCmd, const vk::Image& other, vk::ImageLayout prevLayout, const vk::Device& d, const vk::Queue& q, const vk::Fence* fence, const vk::Semaphore* waitSema, const vk::Semaphore* signalSema, vk::Extent3D ex, vk::Offset3D off, vk::ImageAspectFlagBits aspect) {
		vk::ImageCopy region {
			vk::ImageSubresourceLayers { aspect, 0, 0, 1 },
				off,
				vk::ImageSubresourceLayers { aspect, 0, 0, 1 },
				vk::Offset3D{},
				ex,
		};
		/*
    VULKAN_HPP_CONSTEXPR BufferImageCopy( VULKAN_HPP_NAMESPACE::DeviceSize             bufferOffset_      = {},
                                          uint32_t                                     bufferRowLength_   = {},
                                          uint32_t                                     bufferImageHeight_ = {},
                                          VULKAN_HPP_NAMESPACE::ImageSubresourceLayers imageSubresource_  = {},
                                          VULKAN_HPP_NAMESPACE::Offset3D               imageOffset_       = {},
                                          VULKAN_HPP_NAMESPACE::Extent3D imageExtent_ = {} ) VULKAN_HPP_NOEXCEPT
		*/
		vk::BufferImageCopy copyInfo {
				0,
				{}, {}, // Inferred from image
				vk::ImageSubresourceLayers { aspect, 0, 0, 1 },
				off, ex
		};

		copyCmd.begin(vk::CommandBufferBeginInfo{});

		std::vector<vk::ImageMemoryBarrier> imgBarriers = {
			vk::ImageMemoryBarrier {
				{},{},
				// {}, vk::ImageLayout::eTransferSrcOptimal,
				// vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal,
				prevLayout, vk::ImageLayout::eTransferSrcOptimal,
				{}, {},
				other,
				vk::ImageSubresourceRange { aspect, 0, 1, 0, 1}
			}
		};
		copyCmd.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllGraphics,
				// vk::PipelineStageFlagBits::eAllGraphics,
				vk::PipelineStageFlagBits::eHost,
				vk::DependencyFlagBits::eDeviceGroup,
				{}, {}, imgBarriers);

		copyCmd.copyImageToBuffer(other, vk::ImageLayout::eTransferSrcOptimal, *buffer, {1,&copyInfo});
		copyCmd.end();

		vk::PipelineStageFlags waitMasks[1] = {vk::PipelineStageFlagBits::eAllGraphics};
		uint32_t wait_semas = waitSema == nullptr ? 0 : 1;
		uint32_t signal_semas = signalSema == nullptr ? 0 : 1;
		vk::SubmitInfo submitInfo {
				{wait_semas, waitSema}, // wait sema
				{wait_semas, waitMasks},
				{1u, &copyCmd},
				{signal_semas, signalSema} // signal sema
		};
		uint32_t nfence = fence == nullptr ? 0 : 1;
		if (nfence) {
			q.submit(submitInfo, *fence);
			d.waitForFences({nfence, fence}, true, 999999999999);
			d.resetFences({nfence, fence});
		} else
			q.submit(submitInfo);

		return false;
}



/* ===================================================
 *
 *
 *                  ExMesh
 *
 *
 * =================================================== */


VertexInputDescription MeshDescription::getVertexDescription() {
	VertexInputDescription description;


	// Position will be stored at Location 0
	uint64_t offset = 0;
	vk::VertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	if (posDims != 4 and posDims != 2 and posDims != 3) throw std::runtime_error("posDims must be 2, 3, or 4, but was " + std::to_string(posDims));


	if (posDims == 4) {
		if (posType == MeshDescription::ScalarType::Float) positionAttribute.format = vk::Format::eR32G32B32A32Sfloat;
		if (posType == MeshDescription::ScalarType::UInt8) positionAttribute.format = vk::Format::eR8G8B8A8Unorm;
		if (posType == MeshDescription::ScalarType::UInt16) positionAttribute.format = vk::Format::eR16G16B16A16Unorm;
		if (posType == MeshDescription::ScalarType::UInt32) positionAttribute.format = vk::Format::eR32G32B32A32Uint;
		if (posType == MeshDescription::ScalarType::UInt8_scaled) positionAttribute.format = vk::Format::eR8G8B8A8Uscaled;
		if (posType == MeshDescription::ScalarType::UInt16_scaled) positionAttribute.format = vk::Format::eR16G16B16A16Uscaled;
	} else {
		if (posType == MeshDescription::ScalarType::Float)
			positionAttribute.format = posDims == 2 ? vk::Format::eR32G32Sfloat : vk::Format::eR32G32B32Sfloat;
		if (posType == MeshDescription::ScalarType::UInt8)
			positionAttribute.format = posDims == 2 ? vk::Format::eR8G8B8Unorm : vk::Format::eR8G8Unorm;
		if (posType == MeshDescription::ScalarType::UInt16)
			positionAttribute.format = posDims == 3 ? vk::Format::eR16G16B16Unorm : vk::Format::eR16G16Unorm;
		if (posType == MeshDescription::ScalarType::UInt32)
			positionAttribute.format = posDims == 3 ? vk::Format::eR32G32B32Uint : vk::Format::eR32G32Uint;

		if (posType == MeshDescription::ScalarType::UInt8_scaled)
			positionAttribute.format = posDims == 3 ? vk::Format::eR8G8B8Uscaled : vk::Format::eR8G8Uscaled;
		if (posType == MeshDescription::ScalarType::UInt16_scaled)
			positionAttribute.format = posDims == 3 ? vk::Format::eR16G16B16Uscaled : vk::Format::eR16G16Uscaled;
	}
	positionAttribute.offset = offset;
	fmt::print(" - [vertex desc] pos offset {}\n", offset);
	offset += posDims * typeToSize(posType);
	description.attributes.push_back(positionAttribute);


	// uv will be stored at Location 1
	if (haveUvs) {
		vk::VertexInputAttributeDescription uvAttribute = {};
		uvAttribute.binding = 0;
		uvAttribute.location = 1;

		if (uvType == MeshDescription::ScalarType::Float)
			uvAttribute.format = vk::Format::eR32G32Sfloat;
		if (uvType == MeshDescription::ScalarType::UInt8)
			uvAttribute.format = vk::Format::eR8G8Unorm;
		if (uvType == MeshDescription::ScalarType::UInt16)
			uvAttribute.format = vk::Format::eR16G16Unorm;
		if (uvType == MeshDescription::ScalarType::UInt32)
			uvAttribute.format = vk::Format::eR32G32Uint;
		if (uvType == MeshDescription::ScalarType::UInt8_scaled)
			uvAttribute.format = vk::Format::eR8G8Uscaled;
		if (uvType == MeshDescription::ScalarType::UInt16_scaled)
			uvAttribute.format = vk::Format::eR16G16Uscaled;

		uvAttribute.offset = offset;
		fmt::print(" - [vertex desc] uv offset {}\n", offset);
		offset += 2 * typeToSize(uvType);
		description.attributes.push_back(uvAttribute);
	}

	 // Normal will be stored at Location 2
	if (haveNormals) {
		vk::VertexInputAttributeDescription normalAttribute = {};
		normalAttribute.binding = 0;
		normalAttribute.location = 2;

		if (normalType == MeshDescription::ScalarType::Float)
			normalAttribute.format = vk::Format::eR32G32B32Sfloat;
		if (normalType == MeshDescription::ScalarType::UInt8)
			normalAttribute.format = vk::Format::eR8G8B8Unorm;
		if (normalType == MeshDescription::ScalarType::UInt16)
			normalAttribute.format = vk::Format::eR16G16B16Unorm;
		if (normalType == MeshDescription::ScalarType::UInt32)
			normalAttribute.format = vk::Format::eR32G32B32Uint;
		if (normalType == MeshDescription::ScalarType::UInt8_scaled)
			normalAttribute.format = vk::Format::eR8G8B8Uscaled;
		if (normalType == MeshDescription::ScalarType::UInt16_scaled)
			normalAttribute.format = vk::Format::eR16G16B16Uscaled;

		normalAttribute.offset = offset;
		fmt::print(" - [vertex desc] normal offset {}\n", offset);
		offset += 3 * typeToSize(normalType);
		description.attributes.push_back(normalAttribute);
	}

	// auto oldRowSize = rowSize;
	// rowSize = offset;
	// offset = 12;
	// fmt::print(" - MeshDescription: row size {}/{}\n", rowSize,oldRowSize);
	int extraPadding = 0;
	while (offset % 4 != 0) {
		extraPadding++;
		offset++;
	}
	if (extraPadding) fmt::print(" - [MeshDescription::getVertexDescription] Warning: added {} pad bytes to align to 4.\n", extraPadding);

	vk::VertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = offset;
	mainBinding.inputRate = vk::VertexInputRate::eVertex;
	description.bindings.push_back(mainBinding);

	return description;
}

/*
ResidentMesh::~ResidentMesh() {
	freeCpu();
}

void ResidentMesh::freeCpu() {
	if (verts) free(verts);
	if (inds) free(inds);
	verts = 0;
	inds = 0;
}

void ResidentMesh::fill(
		int posDims_,
		const std::vector<float>& pos,
		const std::vector<float>& uvs,
		const std::vector<float>& normals,
		const std::vector<IndType>& inds_) {
	this->posDims = posDims_;
	assert(pos.size() > 0);
	assert(pos.size() % posDims == 0);
	assert(pos.size()/posDims == uvs.size()/2 or uvs.size() == 0);
	assert(pos.size()/posDims == normals.size()/3 or normals.size() == 0);
	rows = pos.size()/posDims;

	rowSize = 4 * (posDims + (uvs.size()?2:0) + (uvs.size()?3:0));

	freeCpu();
	if (normals.size()) haveNormals = true;
	if (uvs.size()) haveUvs = true;
	verts = (float*) aligned_alloc(16, size());
	int j = 0;
	for (int i=0; i<pos.size()/posDims; i++) {
		for (int k=0; k<posDims; k++)
			verts[j++] = pos[i*posDims+k];
		if (uvs.size()) {
			verts[j++] = uvs[i*2+0];
			verts[j++] = uvs[i*2+1];
		}
		if (normals.size()) {
			verts[j++] = normals[i*3+0];
			verts[j++] = normals[i*3+1];
			verts[j++] = normals[i*3+2];
		}
	}

	ninds = inds_.size();
	if (ninds) {
		inds = (IndType*) aligned_alloc(16, sizeInds());
		for (int i=0; i<ninds; i++) this->inds[i] = inds_[i];
	}
}

void ResidentMesh::createAndUpload(vk::raii::Device& d, const vk::PhysicalDevice& pd, const std::vector<uint32_t>& queueFamilyIndices) {
	assert(size() > 0);
	vertBuffer.setAsVertexBuffer(size(), true);
	vertBuffer.create(d, pd, queueFamilyIndices);
	vertBuffer.upload(verts, size());
	if (ninds > 0) {
		indBuffer.setAsIndexBuffer(sizeInds(), true);
		indBuffer.create(d, pd, queueFamilyIndices);
		indBuffer.upload(inds, sizeInds());
	}

	freeCpu();
}
void ResidentMesh::createAndUpload(Uploader& uploader) {
	assert(size() > 0);

	BaseVkApp* app = uploader.app;
	vertBuffer.setAsVertexBuffer(size(), false);
	vertBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
	//vertBuffer.upload(verts.data(), size());
	//uploader.uploadSync(vertBuffer, verts.data(), size(), 0);
	uploader.uploadSync(vertBuffer, verts, size(), 0);
	if (ninds > 0) {
		indBuffer.setAsIndexBuffer(sizeInds(), false);
		indBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		//indBuffer.upload(inds.data(), sizeInds());
		//uploader.uploadSync(indBuffer, inds.data(), sizeInds(), 0);
		uploader.uploadSync(indBuffer, inds, sizeInds(), 0);
	}

	freeCpu();
}
*/


/* ===================================================
 *
 *
 *                  ExImage
 *
 *
 * =================================================== */


void ExImage::createAsDepthBuffer(Uploader& uploader, int h, int w, bool cpuVisible, vk::ImageUsageFlagBits extraFlags) {
	extent = vk::Extent3D { (uint32_t)w, (uint32_t)h, 1 };
	aspectFlags = vk::ImageAspectFlagBits::eDepth;
	if (cpuVisible) {
		usageFlags = extraFlags;
		format = vk::Format::eD32Sfloat;
		memPropFlags = vk::MemoryPropertyFlagBits::eHostVisible;
		if (viewFormat == vk::Format::eUndefined) viewFormat = vk::Format::eD32Sfloat;
	} else {
		usageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment | extraFlags | vk::ImageUsageFlagBits::eTransferSrc;
		format = vk::Format::eD32Sfloat;
		memPropFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
		if (viewFormat == vk::Format::eUndefined) viewFormat = format;
	}
	create_(uploader);
}
void ExImage::createAsTexture(Uploader& uploader, int h, int w, vk::Format f, uint8_t* data, vk::ImageUsageFlags extraFlags, vk::SamplerAddressMode addr) {
	extent = vk::Extent3D { (uint32_t)w, (uint32_t)h, 1 };
	format = f;
	if (viewFormat == vk::Format::eUndefined) viewFormat = f;
	usageFlags = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | extraFlags;
	aspectFlags = vk::ImageAspectFlagBits::eColor;
	memPropFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;

	create_(uploader);

	vk::SamplerCreateInfo samplerInfo {};
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeV = samplerInfo.addressModeU = addr;
	samplerInfo.unnormalizedCoordinates = unnormalizedCoordinates;
	sampler = std::move(vk::raii::Sampler{uploader.app->deviceGpu, samplerInfo});

	if (data != nullptr)
		uploader.uploadSync(*this, data, size(), 0);
}

void ExImage::createAsCpuVisible(Uploader& uploader, int h, int w, vk::Format f, uint8_t* data, vk::ImageUsageFlags extraFlags, vk::SamplerAddressMode addr) {
	extent = vk::Extent3D { (uint32_t)w, (uint32_t)h, 1 };
	format = f;
	if (viewFormat == vk::Format::eUndefined) viewFormat = f;
	usageFlags = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
	aspectFlags = vk::ImageAspectFlagBits::eColor;
	// memPropFlags = vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached;
	memPropFlags = vk::MemoryPropertyFlagBits::eHostVisible;

	create_(uploader);

	vk::SamplerCreateInfo samplerInfo {};
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeV = samplerInfo.addressModeU = addr;
	samplerInfo.unnormalizedCoordinates = unnormalizedCoordinates;
	sampler = std::move(vk::raii::Sampler{uploader.app->deviceGpu, samplerInfo});

	if (data != nullptr)
		uploader.uploadSync(*this, data, size(), 0);
}

void ExImage::create_(Uploader& uploader) {

	vk::raii::Device& d = uploader.app->deviceGpu;
	vk::PhysicalDevice pd = *uploader.app->pdeviceGpu;

	layout = vk::ImageLayout::eUndefined;

	// Image
	vk::ImageCreateInfo imageInfo = { };
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = format;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = memPropFlags == vk::MemoryPropertyFlagBits::eHostVisible ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
    imageInfo.usage = usageFlags;

	image = std::move(d.createImage(imageInfo));

	auto reqs = image.getMemoryRequirements();
	uint64_t size_ = reqs.size;
	// auto memPropFlags_ = memPropFlags | ((vk::Flags<vk::MemoryPropertyFlagBits>) reqs.memoryTypeBits);
	uint32_t memMask = reqs.memoryTypeBits;
	auto memPropFlags_ = memPropFlags;
	//printf(" - image computed size %lu, vulkan given size %lu\n", size_);

	// Memory
	uint32_t idx = 0;
	uint64_t minSize = std::max(size_, ((size()+0x1000-1)/0x1000)*0x1000);
	idx = findMemoryTypeIndex(pd, memPropFlags_, memMask);
	// printf(" - creating image buffers to memory type idx %u\n", idx);
	vk::MemoryAllocateInfo allocInfo { std::max(minSize,size()), idx };
	mem = std::move(vk::raii::DeviceMemory(d, allocInfo));

	image.bindMemory(*mem, 0);

	// ImageView
	// Only create if usageFlags is compatible
	if (
			(usageFlags &  vk::ImageUsageFlagBits::eSampled) or
			(usageFlags &  vk::ImageUsageFlagBits::eColorAttachment) or
			(usageFlags &  vk::ImageUsageFlagBits::eDepthStencilAttachment) or
			(usageFlags &  vk::ImageUsageFlagBits::eStorage)) {
		vk::ImageViewCreateInfo viewInfo = {};
		viewInfo.viewType = vk::ImageViewType::e2D;
		viewInfo.image = *image;
		viewInfo.format = viewFormat;
		// viewInfo.format = format == vk::Format::eR8Uint ? vk::Format::eR8G8B8A8Uint : format;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.aspectMask = aspectFlags;

		view = std::move(d.createImageView(viewInfo));
	}

	//return false;
}

void ExImage::createAsStorage(vk::raii::Device& d, vk::raii::PhysicalDevice& pd, int h, int w, vk::Format f, vk::ImageUsageFlags extraFlags, vk::SamplerAddressMode addr) {
	extent = vk::Extent3D { (uint32_t)w, (uint32_t)h, 1 };
	format = f;
	if (viewFormat == vk::Format::eUndefined) viewFormat = f;
	usageFlags = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | extraFlags;
	aspectFlags = vk::ImageAspectFlagBits::eColor;
	memPropFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
	// memPropFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
	// memPropFlags = vk::MemoryPropertyFlagBits::eHostVisible;

	{
		vk::ImageCreateInfo imageInfo = { };
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = format;
		imageInfo.extent = extent;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = memPropFlags == vk::MemoryPropertyFlagBits::eHostVisible ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
		imageInfo.usage = usageFlags;

		image = std::move(d.createImage(imageInfo));

		auto reqs = image.getMemoryRequirements();
		uint64_t size_ = reqs.size;
		uint32_t memMask = reqs.memoryTypeBits;
		auto memPropFlags_ = memPropFlags;

		// Memory
		uint32_t idx = 0;
		uint64_t minSize = std::max(size_, ((size()+0x1000-1)/0x1000)*0x1000);
		idx = findMemoryTypeIndex(*pd, memPropFlags_, memMask);
		vk::MemoryAllocateInfo allocInfo { std::max(minSize,size()), idx };
		mem = std::move(vk::raii::DeviceMemory(d, allocInfo));

		image.bindMemory(*mem, 0);

		// ImageView
		// Only create if usageFlags is compatible
		if (
				(usageFlags &  vk::ImageUsageFlagBits::eSampled) or
				(usageFlags &  vk::ImageUsageFlagBits::eColorAttachment) or
				(usageFlags &  vk::ImageUsageFlagBits::eDepthStencilAttachment) or
				(usageFlags &  vk::ImageUsageFlagBits::eStorage)) {
			vk::ImageViewCreateInfo viewInfo = {};
			viewInfo.viewType = vk::ImageViewType::e2D;
			viewInfo.image = *image;
			viewInfo.format = viewFormat;
			// viewInfo.format = format == vk::Format::eR8Uint ? vk::Format::eR8G8B8A8Uint : format;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;
			viewInfo.subresourceRange.aspectMask = aspectFlags;

			view = std::move(d.createImageView(viewInfo));
		}
	}

	vk::SamplerCreateInfo samplerInfo {};
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeV = samplerInfo.addressModeU = addr;
	samplerInfo.unnormalizedCoordinates = unnormalizedCoordinates;
	sampler = std::move(vk::raii::Sampler{d, samplerInfo});
}

bool ExImage::copyFrom(const vk::CommandBuffer &copyCmd, const vk::Image& srcImg,  vk::ImageLayout prevLayout, const vk::Device& d, const vk::Queue& q, const vk::Fence* fence, const vk::Semaphore* waitSema, const vk::Semaphore* signalSema, vk::Extent3D ex, vk::Offset3D off, vk::ImageAspectFlagBits aspect) {
		vk::ImageCopy region {
			vk::ImageSubresourceLayers { aspect, 0, 0, 1 },
				off,
				vk::ImageSubresourceLayers { aspect, 0, 0, 1 },
				vk::Offset3D{},
				ex,
		};

		copyCmd.begin(vk::CommandBufferBeginInfo{});

		std::vector<vk::ImageMemoryBarrier> imgBarriers = {
			vk::ImageMemoryBarrier {
				{},{},
				prevLayout, vk::ImageLayout::eTransferSrcOptimal,
				{}, {},
				srcImg,
				vk::ImageSubresourceRange { aspect, 0, 1, 0, 1}
			},
			vk::ImageMemoryBarrier {
				{},{},
				{}, vk::ImageLayout::eTransferDstOptimal,
				{}, {},
				*this->image,
				vk::ImageSubresourceRange { aspect, 0, 1, 0, 1}
			}
		};
		copyCmd.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllGraphics,
				vk::PipelineStageFlagBits::eAllGraphics,
				vk::DependencyFlagBits::eDeviceGroup,
				{}, {}, imgBarriers);

		copyCmd.copyImage(srcImg, vk::ImageLayout::eTransferSrcOptimal, *this->image, vk::ImageLayout::eTransferDstOptimal, {1,&region});
		copyCmd.end();

		vk::PipelineStageFlags waitMasks[1] = {vk::PipelineStageFlagBits::eAllGraphics};
		uint32_t wait_semas = waitSema == nullptr ? 0 : 1;
		uint32_t signal_semas = signalSema == nullptr ? 0 : 1;
		vk::SubmitInfo submitInfo {
				{wait_semas, waitSema}, // wait sema
				{wait_semas, waitMasks},
				{1u, &copyCmd},
				{signal_semas, signalSema} // signal sema
		};
		uint32_t nfence = fence == nullptr ? 0 : 1;
		if (nfence) {
			q.submit(submitInfo, *fence);
			d.waitForFences({nfence, fence}, true, 999999999999);
			d.resetFences({nfence, fence});
		} else
			q.submit(submitInfo);

		return false;
}

vk::ImageMemoryBarrier ExImage::barrierTo(vk::ImageLayout to, const ImageBarrierDetails& details) {
	layout = to;
	return
			vk::ImageMemoryBarrier {
				{},{},
				layout, to,
				{}, {},
				*this->image,
				vk::ImageSubresourceRange { aspect, 0, 1, 0, 1}
			}

}


/* ===================================================
 *
 *
 *                  Uploader
 *
 *
 * =================================================== */


Uploader::Uploader(BaseVkApp* app_, QueueDeviceSpec& qds_) : app(app_), qds(qds_)
{
	fence = std::move(app->deviceGpu.createFence({}));

	vk::CommandPoolCreateInfo poolInfo {
		vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		app->queueFamilyGfxIdxs[0] };
	pool = std::move(app->deviceGpu.createCommandPool(poolInfo));

	vk::CommandBufferAllocateInfo bufInfo {
		*pool,
		vk::CommandBufferLevel::ePrimary,
		1 };
	cmd = std::move(app->deviceGpu.allocateCommandBuffers(bufInfo)[0]);
}

void Uploader::uploadScratch(void* data, size_t len) {
	if (len > scratchBuffer.residentSize) {
			scratchBuffer.setAsBuffer(len, true, vk::BufferUsageFlagBits::eStorageBuffer);
			scratchBuffer.usageFlags = scratchFlags;
			scratchBuffer.create(qds);
		//}
	}
	if (data)
		scratchBuffer.uploadNow(data, len);
}

void Uploader::uploadSync(ExBuffer& dstBuffer, void *data, uint64_t len, uint64_t off) {
	assert(app != nullptr);

	vk::CommandBufferBeginInfo beginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit, {} };
	//vk::CommandBufferBeginInfo beginInfo { {}, {} };

	/*
	ExBuffer tmpBuffer;
	//tmpBuffer.setAsUniformBuffer(len, true);
	tmpBuffer.setAsStorageBuffer(len, true);
	tmpBuffer.usageFlags = vk::BufferUsageFlagBits::eTransferSrc;
	tmpBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
	tmpBuffer.upload(data, len);
	*/
	uploadScratch(data, len);

	vk::BufferCopy regions[1] = { { 0, 0, len } };

	cmd.reset();
	cmd.begin(beginInfo);
	cmd.copyBuffer(*scratchBuffer.buffer, *dstBuffer.buffer, {1,regions});
	cmd.end();
	//printf(" - [uploadSync q%p] copying %lu bytes.\n", (void*)(VkQueue)(q), regions[0].size);

	//vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllCommands;
	//vk::SubmitInfo submitInfo { nullptr, {1,&waitMask}, nullptr, nullptr, };
	vk::SubmitInfo submitInfo { nullptr, nullptr, {1,&*cmd}, nullptr };
	qds.q.submit(submitInfo, *fence);
	app->deviceGpu.waitForFences({1, &*fence}, true, 9999999999);
	app->deviceGpu.resetFences({1, &*fence});
}

void Uploader::uploadSync(ExImage& dstImage, void *data, uint64_t len, uint64_t off) {
	assert(app != nullptr);
	assert(data != nullptr);

	vk::CommandBufferBeginInfo beginInfo { vk::CommandBufferUsageFlagBits::eOneTimeSubmit, {} };
	//vk::CommandBufferBeginInfo beginInfo { {}, {} };

	/*
	ExBuffer tmpBuffer;
	//tmpBuffer.setAsUniformBuffer(len, true);
	tmpBuffer.setAsStorageBuffer(len, true);
	tmpBuffer.usageFlags = vk::BufferUsageFlagBits::eTransferSrc;
	tmpBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
	tmpBuffer.upload(data, len);
	*/
	uploadScratch(data, len);

	vk::ImageSubresourceLayers subres {
		vk::ImageAspectFlagBits::eColor,
			0,
			0,
			1 };
	vk::ImageSubresourceRange subresRange {
		vk::ImageAspectFlagBits::eColor,
		0, 1, 0, 1 };


	vk::BufferImageCopy regions[1] = {
		{ 0, dstImage.extent.width, dstImage.extent.height, subres, vk::Offset3D{}, dstImage.extent }
	};

	/*
      ImageMemoryBarrier( VULKAN_HPP_NAMESPACE::AccessFlags srcAccessMask_ = {},
                          VULKAN_HPP_NAMESPACE::AccessFlags dstAccessMask_ = {},
                          VULKAN_HPP_NAMESPACE::ImageLayout oldLayout_ = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined,
                          VULKAN_HPP_NAMESPACE::ImageLayout newLayout_ = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined,
                          uint32_t                          srcQueueFamilyIndex_        = {},
                          uint32_t                          dstQueueFamilyIndex_        = {},
                          VULKAN_HPP_NAMESPACE::Image       image_                      = {},
                          VULKAN_HPP_NAMESPACE::ImageSubresourceRange subresourceRange_ = {} ) VULKAN_HPP_NOEXCEPT
						  */

	// Transition into/out-of layout for copy
	vk::ImageMemoryBarrier barrierIn {
		{}, vk::AccessFlagBits::eTransferWrite,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eTransferDstOptimal,
			0,0,
			*dstImage.image,
			subresRange };
	vk::ImageMemoryBarrier barrierOut {
		{}, vk::AccessFlagBits::eShaderRead,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			0,0,
			*dstImage.image,
			subresRange };

	cmd.reset();
	cmd.begin(beginInfo);
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, {1,&barrierIn});
	cmd.copyBufferToImage(*scratchBuffer.buffer, *dstImage.image, vk::ImageLayout::eTransferDstOptimal, {1,regions});
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, {1,&barrierOut});
	cmd.end();

	vk::SubmitInfo submitInfo { nullptr, nullptr, {1,&*cmd}, nullptr };
	qds.q.submit(submitInfo, *fence);
	app->deviceGpu.waitForFences({1, &*fence}, true, 99999999999);
	app->deviceGpu.resetFences({1, &*fence});
}
