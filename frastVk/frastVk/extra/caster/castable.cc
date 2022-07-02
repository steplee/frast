#include "castable.h"

namespace {
	uint32_t UBO_BINDING     = 0;
	uint32_t TEXTURE_BINDING = 1;
}

Castable::~Castable() {
	if (device) sampler.destroy(*device);
}

void Castable::do_init_caster_stuff(Device& device_, uint32_t queueNumberForUploader, TheDescriptorPool& dpool) {
	device = &device_;
	casterStuff.casterMask = 0;

	queue = std::move(Queue{*device, device->queueFamilyGfxIdxs[0], (int)queueNumberForUploader});

	uploader.create(device, &queue);

	sampler.create(*device, VkSamplerCreateInfo{
			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			nullptr, 0,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			VK_SAMPLER_MIPMAP_MODE_NEAREST,
			// VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,

			0.f,
			VK_FALSE, 0.f,
			VK_FALSE, VK_COMPARE_OP_NEVER,
			0, 0,
			VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			false
	});

	/*
		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
			vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 2 },
		};
		vk::DescriptorPoolCreateInfo poolInfo {
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
				1,
				(uint32_t)poolSizes.size(), poolSizes.data()
		};
		casterStuff.casterDescPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

		casterStuff.casterBuffer.setAsUniformBuffer(sizeof(CasterBuffer), true);
		casterStuff.casterBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);
		// Initialize caster buffer as all zeros
		CasterBuffer* dbuf = (CasterBuffer*) casterStuff.casterBuffer.mem.mapMemory(0, sizeof(CasterBuffer), {});
		memset(dbuf, 0, sizeof(CasterBuffer));
		float c2[4] = {0.7, 1., .7, 1.};
		float c1[4] = {0.7, .7, 1., 1.};
		memcpy(dbuf->color1, c1, 4*4);
		memcpy(dbuf->color2, c2, 4*4);
		casterStuff.casterBuffer.mem.unmapMemory();

		// Setup bindings. There is one for the casterData, and one for the casterImages.
		{
			std::vector<vk::DescriptorSetLayoutBinding> bindings;
			// casterData
			bindings.push_back({
					0, vk::DescriptorType::eUniformBuffer,
					1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment });
			// casterImages binding
			bindings.push_back({
					1, vk::DescriptorType::eCombinedImageSampler,
					// 2, vk::ShaderStageFlagBits::eFragment });
					1, vk::ShaderStageFlagBits::eFragment });

			vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
			casterStuff.casterDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

			vk::DescriptorSetAllocateInfo allocInfo {
				*casterStuff.casterDescPool, 1, &*casterStuff.casterDescSetLayout
			};
			casterStuff.casterDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

			// descSet is allocated, now make the arrays point correctly on the gpu side.
			std::vector<vk::DescriptorImageInfo> i_infos;
			std::vector<vk::DescriptorBufferInfo> b_infos;

			b_infos.push_back(vk::DescriptorBufferInfo{
					*casterStuff.casterBuffer.buffer, 0, VK_WHOLE_SIZE
					});

			// for (int j=0; j<2; j++) {
			for (int j=0; j<1; j++) {
				i_infos.push_back(vk::DescriptorImageInfo{
						*casterStuff.casterImages[j].sampler,
						*casterStuff.casterImages[j].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
				});
			}

			std::vector<vk::WriteDescriptorSet> writeDesc = {
				{
					*casterStuff.casterDescSet,
					0, 0, (uint32_t)b_infos.size(),
					vk::DescriptorType::eUniformBuffer,
					nullptr,
					b_infos.data(),
					nullptr
				}
			};
			app->deviceGpu.updateDescriptorSets({(uint32_t)writeDesc.size(), writeDesc.data()}, nullptr);
		}
		*/


	// We cannot set the texture yet nor write its descriptor,
	// because we don't know its size. That is done in the setCasterInRenderThread()

	// void set(uint32_t size, VkMemoryPropertyFlags memFlags, VkBufferUsageFlags bufFlags);
	casterStuff.casterBuffer.set(sizeof(CasterBuffer),
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	casterStuff.casterBuffer.create(*device);

	CasterBuffer *dbuf = (CasterBuffer*) casterStuff.casterBuffer.map();
	memset(dbuf, 0, sizeof(CasterBuffer));
	float c2[4] = {0.7, 1., .7, 1.};
	float c1[4] = {0.7, .7, 1., 1.};
	memcpy(dbuf->color1, c1, 4*4);
	memcpy(dbuf->color2, c2, 4*4);

	// bindings: there is one set with two bindings: one for UBO, one for image

	uint32_t uboBinding = casterStuff.dset.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT);
	uint32_t imgBinding = casterStuff.dset.addBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT);
	assert(uboBinding == UBO_BINDING);
	assert(imgBinding == TEXTURE_BINDING);
	casterStuff.dset.create(dpool, {&casterStuff.pipeline});
	
	VkDescriptorBufferInfo bufferInfo { casterStuff.casterBuffer, 0, sizeof(CasterBuffer) };
	casterStuff.dset.update(VkWriteDescriptorSet{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
			casterStuff.dset, UBO_BINDING,
			0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			nullptr, &bufferInfo, nullptr
			});

}

// void Castable::setCasterInRenderThread(CasterWaitingData& cwd) {
void Castable::setCasterInRenderThread() {
	if (not cwd.isNew()) return;

	// Update cpu mask
	casterStuff.casterMask = cwd.mask;

	/*
	// Upload texture
	if (cwd.image.w > 0 and cwd.image.h > 0) {
		// If texture is new size, must create it then write descSet
		if (casterStuff.casterImages[0].extent.width != cwd.image.w or casterStuff.casterImages[0].extent.height != cwd.image.h) {
			// dprint0(" - [setCaster] new image size {} {} {} old {} {}\n", cwd.image.w, cwd.image.h, cwd.image.channels(), casterStuff.casterImages[0].extent.width, casterStuff.casterImages[0].extent.height);
			if (cwd.image.format == Image::Format::GRAY)
				casterStuff.casterImages[0].createAsTexture(app->uploader, cwd.image.h, cwd.image.w, vk::Format::eR8Unorm, cwd.image.buffer);
			else
				casterStuff.casterImages[0].createAsTexture(app->uploader, cwd.image.h, cwd.image.w, vk::Format::eR8G8B8A8Unorm, cwd.image.buffer);

			std::vector<vk::DescriptorImageInfo> i_infos = {
				vk::DescriptorImageInfo{
					*casterStuff.casterImages[0].sampler,
					*casterStuff.casterImages[0].view,
					vk::ImageLayout::eShaderReadOnlyOptimal
					}};
			vk::WriteDescriptorSet writeDesc = {
				*casterStuff.casterDescSet,
				1, 0, (uint32_t)i_infos.size(),
				vk::DescriptorType::eCombinedImageSampler,
				i_infos.data(),
				nullptr,
				nullptr
			};
			app->deviceGpu.updateDescriptorSets({1, &writeDesc}, nullptr);
			casterStuff.casterTextureSet = true;
		} else {
			app->uploader.uploadSync(casterStuff.casterImages[0], cwd.image.buffer, cwd.image.w*cwd.image.h*cwd.image.channels(), 0);
		}
	}

	// Upload UBO
	{
		CasterBuffer* cameraBuffer = (CasterBuffer*) casterStuff.casterBuffer.mem.mapMemory(0, sizeof(CasterBuffer), {});
		cameraBuffer->casterMask = cwd.mask;
		if (cwd.haveMatrix1) memcpy(cameraBuffer->casterMatrix   , cwd.casterMatrix1, sizeof(float)*16);
		if (cwd.haveMatrix2) memcpy(cameraBuffer->casterMatrix+16, cwd.casterMatrix2, sizeof(float)*16);
		if (cwd.haveColor1) memcpy(cameraBuffer->color1   , cwd.color1, sizeof(float)*4);
		if (cwd.haveColor2) memcpy(cameraBuffer->color2, cwd.color2, sizeof(float)*4);
		casterStuff.casterBuffer.mem.unmapMemory();
		cwd.haveMatrix1 = false;
		cwd.haveMatrix2 = false;
		cwd.haveColor1 = false;
		cwd.haveColor2 = false;
	}
	*/

	// void enqueueUpload(ExBuffer& buffer, void* data, uint64_t len, uint64_t off);
	// void enqueueUpload(ExImage& img,  void* data, uint64_t len, uint64_t off, VkImageLayout finalLayout);
	// void set(VkExtent2D extent, VkFormat fmt, VkMemoryPropertyFlags memFlags, VkImageUsageFlags usageFlags, VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT);

	// 1) Update image
	if (cwd.image.w > 0 and cwd.image.h > 0) {
		// If texture is new size, must create it then write descSet
		casterStuff.casterTextureSet = true;
		if (casterStuff.casterImage.extent.width != cwd.image.w or casterStuff.casterImage.extent.height != cwd.image.h) {
			fmt::print(" - [setCaster] new image size {} {} {} old {} {}\n", cwd.image.w, cwd.image.h, cwd.image.channels(), casterStuff.casterImage.extent.width, casterStuff.casterImage.extent.height);
			if (cwd.image.format == Image::Format::GRAY)
				casterStuff.casterImage.set({(uint32_t)cwd.image.w,(uint32_t)cwd.image.h}, VK_FORMAT_R8_UNORM,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			else
				casterStuff.casterImage.set({(uint32_t)cwd.image.w,(uint32_t)cwd.image.h}, VK_FORMAT_R8G8B8A8_UNORM,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
						VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

			casterStuff.casterImage.create(*device);

			VkDescriptorImageInfo imgInfo { sampler, casterStuff.casterImage.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
			casterStuff.dset.update(VkWriteDescriptorSet{
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
					casterStuff.dset, TEXTURE_BINDING,
					0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					&imgInfo, 0, nullptr
					});

		}

		uploader.enqueueUpload(casterStuff.casterImage, cwd.image.buffer, cwd.image.w*cwd.image.h*cwd.image.channels(), 0, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
		uploader.execute();
	}

	// 2) Update ubo
	{
		CasterBuffer* cameraBuffer = (CasterBuffer*)casterStuff.casterBuffer.mappedAddr; // we keep it bound
		assert(cameraBuffer);
		cameraBuffer->casterMask = cwd.mask;
		if (cwd.haveMatrix1) memcpy(cameraBuffer->casterMatrix   , cwd.casterMatrix1, sizeof(float)*16);
		if (cwd.haveMatrix2) memcpy(cameraBuffer->casterMatrix+16, cwd.casterMatrix2, sizeof(float)*16);
		if (cwd.haveColor1) memcpy(cameraBuffer->color1   , cwd.color1, sizeof(float)*4);
		if (cwd.haveColor2) memcpy(cameraBuffer->color2, cwd.color2, sizeof(float)*4);
	}

	cwd.setNotNew();

}
