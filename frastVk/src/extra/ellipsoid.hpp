#pragma once

struct EarthEllipsoid {

	inline EarthEllipsoid(BaseVkApp* app) : app(app) { }

		void renderInPass(RenderState& rs, vk::CommandBuffer cmd);
		void init(uint32_t subpass);

	private:

		BaseVkApp* app;
		vk::raii::DescriptorPool descPool = {nullptr};
		vk::raii::DescriptorSetLayout globalDescSetLayout = {nullptr}; // holds camera etc
		vk::raii::DescriptorSet globalDescSet = {nullptr};

		PipelineStuff pipelineStuff;
		ResidentBuffer globalBuffer;
};

inline void EarthEllipsoid::init(uint32_t subpass) {

	{
		globalBuffer.setAsUniformBuffer(18 * 4, true);
		globalBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
	}

	{
		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
		};
		vk::DescriptorPoolCreateInfo poolInfo {
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
				1,
				(uint32_t)poolSizes.size(), poolSizes.data()
		};
		descPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));
	}

	{
		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment } };

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


	{
		pipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		std::string vsrcPath = "../src/shaders/extra/fullScreenQuad.v.glsl";
		std::string fsrcPath = "../src/shaders/extra/earth.f.glsl";
		createShaderFromFiles(app->deviceGpu, pipelineStuff.vs, pipelineStuff.fs, vsrcPath, fsrcPath);

		pipelineStuff.setLayouts.push_back(*globalDescSetLayout);

		PipelineBuilder builder;
		builder.init(
				{},
				vk::PrimitiveTopology::eTriangleList,
				*pipelineStuff.vs, *pipelineStuff.fs);
		pipelineStuff.build(builder, app->deviceGpu, *app->simpleRenderPass.pass, subpass);
	}

}

inline void EarthEllipsoid::renderInPass(RenderState& rs, vk::CommandBuffer cmd) {

	{
		alignas(8) float i_mvp_with_focal[18];
		Eigen::Map<RowMatrix4f> i_mvp { i_mvp_with_focal };
		Eigen::Map<const RowMatrix4d> vi_d { rs.camera->viewInv() };
		i_mvp = vi_d.cast<float>();
		// i_mvp_with_focal[16] = .5 * rs.camera->spec().fx() / rs.camera->spec().w;
		// i_mvp_with_focal[17] = .5 * rs.camera->spec().fy() / rs.camera->spec().h;
		i_mvp_with_focal[16] = .5 * rs.camera->spec().w / rs.camera->spec().fx();
		i_mvp_with_focal[17] = .5 * rs.camera->spec().h / rs.camera->spec().fy();

		void* dbuf = (void*) globalBuffer.mem.mapMemory(0, 18*4, {});
		memcpy(dbuf, i_mvp_with_focal, 18*4);
		globalBuffer.mem.unmapMemory();
	}

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.draw(6, 1, 0, 0);

}
