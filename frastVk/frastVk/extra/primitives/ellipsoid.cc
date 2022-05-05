#include "ellipsoid.h"
#include "frastVk/core/load_shader.hpp"
#include "frastVk/utils/eigen.h"

void EllipsoidSet::unset(int i) {
	if (residency[i]) n_resident--;
	residency[i] = false;
}
void EllipsoidSet::set(int i, const float matrix[16], const float color[4]) {
	if (not residency[i]) n_resident++;
	residency[i] = true;

	{
		EllipsoidData* dbuf = (EllipsoidData*) globalBuffer.mem.mapMemory(16*4 + sizeof(EllipsoidData)*i, sizeof(EllipsoidData), {});
		memcpy(dbuf->matrix, matrix, 16*4);
		memcpy(dbuf->color, color, 4*4);
		globalBuffer.mem.unmapMemory();
	}

}


void EllipsoidSet::init(uint32_t subpass) {
	{
		globalBuffer.setAsUniformBuffer(16 * 4 + sizeof(EllipsoidData)*maxEllipsoids, true);
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
		loadShader(app->deviceGpu, pipelineStuff.vs, pipelineStuff.fs, "extra/fullScreenQuad", "extra/primEllipsoid");

		pipelineStuff.setLayouts.push_back(*globalDescSetLayout);

		PipelineBuilder builder;
		builder.init(
				{},
				vk::PrimitiveTopology::eTriangleList,
				*pipelineStuff.vs, *pipelineStuff.fs);
		pipelineStuff.build(builder, app->deviceGpu, *app->simpleRenderPass.pass, subpass);
	}
}

void EllipsoidSet::renderInPass(RenderState& rs, vk::CommandBuffer cmd) {
	if (n_resident == 0) return;


	{
		alignas(8) float i_mvp_[16];
		Eigen::Map<RowMatrix4f> i_mvp { i_mvp_ };
		Eigen::Map<const RowMatrix4d> vi_d { rs.camera->viewInv() };
		i_mvp = vi_d.cast<float>();
		i_mvp.block<3,1>(0,0) *= .5 * rs.camera->spec().w / rs.camera->spec().fx();
		i_mvp.block<3,1>(0,1) *= .5 * rs.camera->spec().h / rs.camera->spec().fy();

		void* dbuf = (void*) globalBuffer.mem.mapMemory(0, 16*4, {});
		memcpy(dbuf, i_mvp_, 16*4);
		globalBuffer.mem.unmapMemory();
	}

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);

	for (int i=0; i<maxEllipsoids; i++) {
		if (not residency[i]) continue;

		// Mark instance as i
		cmd.draw(6, 1, 0, i);
	}
}
