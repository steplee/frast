#include "ellipsoid.h"
#include "frastVk/core/fvkShaders.h"
#include "frastVk/utils/eigen.h"

void EllipsoidSet::unset(int i) {
	if (residency[i]) n_resident--;
	residency[i] = false;
}
void EllipsoidSet::set(int i, const float matrix[16], const float color[4]) {
	if (not residency[i]) n_resident++;
	residency[i] = true;

	{
		EllipsoidData* dbuf = (EllipsoidData*) globalBuffer.mappedAddr;
		memcpy(dbuf->matrix, matrix, 16*4);
		memcpy(dbuf->color, color, 4*4);
	}

}


void EllipsoidSet::init() {
	{
		globalBuffer.set(16 * 4 + sizeof(EllipsoidData)*maxEllipsoids,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		globalBuffer.create(app->mainDevice);
		globalBuffer.map();
	}

	uint32_t globalDataBindingIdx = descSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT);
	descSet.create(app->dpool, {&pipeline});

	// Write descriptor
	VkDescriptorBufferInfo bufferInfo { globalBuffer, 0, 16*4 + sizeof(EllipsoidData)*maxEllipsoids };
	descSet.update(VkWriteDescriptorSet{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
			descSet, 0,
			0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			nullptr, &bufferInfo, nullptr
			});

	{
		loadShader(app->mainDevice, pipeline.vs, pipeline.fs, "extra/fullScreenQuad", "extra/primEllipsoid");

		VertexInputDescription vid;
		float viewportXYWH[4] = {
			0,0,
			(float)app->windowWidth,
			(float)app->windowHeight
		};
		PipelineBuilder builder;
		builder.depthTest = true;
		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pipeline.vs, pipeline.fs);

		pipeline.create(app->mainDevice, viewportXYWH, builder, app->simpleRenderPass.pass, 0);
	}
}

void EllipsoidSet::render(RenderState& rs, Command& cmd) {
	if (n_resident == 0) return;


	{
		alignas(8) float i_mvp_[16];
		Eigen::Map<RowMatrix4f> i_mvp { i_mvp_ };
		Eigen::Map<const RowMatrix4d> vi_d { rs.camera->viewInv() };
		i_mvp = vi_d.cast<float>();
		i_mvp.block<3,1>(0,0) *= .5 * rs.camera->spec().w / rs.camera->spec().fx();
		i_mvp.block<3,1>(0,1) *= .5 * rs.camera->spec().h / rs.camera->spec().fy();

		void* dbuf = (void*) globalBuffer.mappedAddr;
		memcpy(dbuf, i_mvp_, 16*4);
	}

	// cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	// cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*descSet}, nullptr);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &descSet.dset, 0, 0);

	for (int i=0; i<maxEllipsoids; i++) {
		if (not residency[i]) continue;

		// Mark instance as i
		// cmd.draw(6, 1, 0, i);
		vkCmdDraw(cmd, 6, 1, 0, i);
	}
}
