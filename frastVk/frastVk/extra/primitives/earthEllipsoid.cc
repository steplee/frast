#include "earthEllipsoid.h"
#include "frastVk/core/fvkShaders.h"
#include "frastVk/utils/eigen.h"

void EarthEllipsoid::init() {
	{
		globalBuffer.set(16*4,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		globalBuffer.create(app->mainDevice);
		globalBuffer.map();
	}

	uint32_t globalDataBindingIdx = descSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT);
	descSet.create(app->dpool, {&pipeline});

	// Write descriptor
	VkDescriptorBufferInfo bufferInfo { globalBuffer, 0, 16*4 };
	descSet.update(VkWriteDescriptorSet{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
			descSet, 0,
			0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			nullptr, &bufferInfo, nullptr
			});

	{
		loadShader(app->mainDevice, pipeline.vs, pipeline.fs, "extra/fullScreenQuad", "extra/earth");

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

void EarthEllipsoid::render(RenderState& rs, Command& cmd) {

	{
		// Instead of passing focal lengths, we can just multiply the rotation part of the inv-view matrix.
		// alignas(8) float i_mvp_with_focal[18];
		alignas(8) float i_mvp_with_focal[16];
		Eigen::Map<RowMatrix4f> i_mvp { i_mvp_with_focal };
		Eigen::Map<const RowMatrix4d> vi_d { rs.camera->viewInv() };
		i_mvp = vi_d.cast<float>();
		// i_mvp_with_focal[16] = .5 * rs.camera->spec().w / rs.camera->spec().fx();
		// i_mvp_with_focal[17] = .5 * rs.camera->spec().h / rs.camera->spec().fy();
		i_mvp.block<3,1>(0,0) *= .5 * rs.camera->spec().w / rs.camera->spec().fx();
		i_mvp.block<3,1>(0,1) *= .5 * rs.camera->spec().h / rs.camera->spec().fy();

		void* dbuf = (void*) globalBuffer.mappedAddr;
		memcpy(dbuf, i_mvp_with_focal, 16*4);
	}


	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &descSet.dset, 0, 0);
	vkCmdDraw(cmd, 6, 1, 0, 0);

}

