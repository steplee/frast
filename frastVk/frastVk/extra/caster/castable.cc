#include "castable.h"


void Castable::do_init_caster_stuff(BaseVkApp* app) {
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
		void* dbuf = (void*) casterStuff.casterBuffer.mem.mapMemory(0, sizeof(CasterBuffer), {});
		memset(dbuf, 0, sizeof(CasterBuffer));
		casterStuff.casterBuffer.mem.unmapMemory();

		// Setup bindings. There is one for the casterData, and one for the casterImages.
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
}

void Castable::setCasterInRenderThread(const CasterWaitingData& cwd, BaseVkApp* app) {
	// Update cpu mask
	casterStuff.casterMask = cwd.mask;

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
		casterStuff.casterBuffer.mem.unmapMemory();
	}
}
