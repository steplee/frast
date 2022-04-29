#include "frustum.h"
#include <fmt/color.h>

#include "frastVk/core/load_shader.hpp"


FrustumSet::FrustumSet(BaseVkApp* app, int nInSet) : app(app), nInSet(nInSet) {
	init();
}

void FrustumSet::init() {
	{
		float z = -1.f, o = 1.f;
		std::vector<float> verts__;
		std::vector<uint16_t> inds_ = {
			0,1,
			1,2,
			2,3,
			0,3,

			4+0,4+1,
			4+1,4+2,
			4+2,4+3,
			4+0,4+3,

			0,4+0,
			1,4+1,
			2,4+2,
			3,4+3,

			8,9
		};
		nInds = inds_.size();

		verts.setAsVertexBuffer(10*nInSet*7 * 4, true, vk::BufferUsageFlagBits::eTransferDst);
		inds.setAsIndexBuffer(inds_.size() * 2, false);
		verts.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		inds.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		app->uploader.uploadSync(inds, inds_.data(), inds_.size() * 2, 0);

		matrices.setAsUniformBuffer((1+nInSet) * 16 * 4, true);
		matrices.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
		modelMatrices.resize(16*nInSet, 0);

		for (int i=0; i<nInSet; i++)
			setIntrin(i, 1,1, 1,1);
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
			0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex } };

		vk::DescriptorSetLayoutCreateInfo layInfo { {}, 1, bindings };
		globalDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

		vk::DescriptorSetAllocateInfo allocInfo { *descPool, 1, &*globalDescSetLayout };
		globalDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

		// Its allocated, now make it point on the gpu side.
		vk::DescriptorBufferInfo binfo {
			*matrices.buffer, 0, VK_WHOLE_SIZE
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
		// std::string vsrcPath = "../frastVk/shaders/frustum/frustum.v.glsl";
		// std::string fsrcPath = "../frastVk/shaders/frustum/frustum.f.glsl";
		// createShaderFromFiles(app->deviceGpu, pipelineStuff.vs, pipelineStuff.fs, vsrcPath, fsrcPath);
		loadShader(app->deviceGpu, pipelineStuff.vs, pipelineStuff.fs, "frustum/frustum");

		pipelineStuff.setLayouts.push_back(*globalDescSetLayout);

		vk::VertexInputAttributeDescription posAttr, colorAttr;
		posAttr.binding = 0;
		posAttr.location = 0;
		posAttr.offset = 0;
		posAttr.format = vk::Format::eR32G32B32Sfloat;
		colorAttr.binding = 0;
		colorAttr.location = 1;
		colorAttr.offset = 3*4;
		colorAttr.format = vk::Format::eR32G32B32A32Sfloat;
		vk::VertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = 7*4;
		mainBinding.inputRate = vk::VertexInputRate::eVertex;
		VertexInputDescription vertexDesc;
		vertexDesc.attributes = { posAttr, colorAttr };
		vertexDesc.bindings = { mainBinding };


		PipelineBuilder builder;
		builder.init(
				vertexDesc,
				vk::PrimitiveTopology::eLineList,
				*pipelineStuff.vs, *pipelineStuff.fs);
		pipelineStuff.build(builder, app->deviceGpu, *app->simpleRenderPass.pass, app->mainSubpass());
	}
}

void FrustumSet::setColor(int n, const float color[4]) {
	Eigen::Vector4f c { color[0], color[1], color[2], color[3] };
	void* dbuf = (void*) verts.mem.mapMemory(10*4*7*n, 10*4*7, {});
	Eigen::Map<RowMatrix<float,10,7>> vs { (float*) dbuf };
	vs.topRightCorner<8,4>().rowwise() = c.transpose();
	Eigen::Vector4f c2 { color[0], color[1], color[2], color[3] * .3f };
	vs.bottomRightCorner<2,4>().rowwise() = c2.transpose();
	verts.mem.unmapMemory();
}

void FrustumSet::setIntrin(int n, float w, float h, float fx, float fy) {
	void* dbuf = (void*) verts.mem.mapMemory(10*4*7*n, 10*4*7, {});
	float z = -1.f, o = 1.f;
	RowMatrix<float,10,3> new_vs; new_vs <<
			z,z,near,
			o,z,near,
			o,o,near,
			z,o,near,

			z,z,far,
			o,z,far,
			o,o,far,
			z,o,far,

			0,0,0,
			0,0, 16000.0 / 6378137.0;
	new_vs.col(0) *= (fx / w);
	new_vs.col(1) *= (fy / h);
	new_vs.block<4,2>(0,0) *= near;
	new_vs.block<4,2>(4,0) *= far;
	Eigen::Map<RowMatrix<float,10,7>> vs { (float*) dbuf };
	vs.leftCols(3) = new_vs;
	verts.mem.unmapMemory();
}

void FrustumSet::setPose(int n, const Eigen::Vector3d& pos, const RowMatrix3d& R) {
	Eigen::Map<RowMatrix4d> model { modelMatrices.data() + n * 16 };
	model.topRightCorner<3,1>() = pos;
	model.topLeftCorner<3,3>() = R;
	model.row(3) << 0,0,0,1;
}




void FrustumSet::renderInPass(RenderState& rs, vk::CommandBuffer cmd) {

	// Setup matrices
	{
		std::vector<float> frameModelMatrices ( (nInSet+1) * 16 );
		Eigen::Map<const RowMatrix4d> vp { rs.mvp() };

		// First is just the viewProj
		Eigen::Map<RowMatrix4f> first { frameModelMatrices.data() }; first = vp.cast<float>();

		// Rest are just models
		for (int i=0; i<nInSet; i++) {
			Eigen::Map<const RowMatrix4d> model { modelMatrices.data() + i * 16 };
			Eigen::Map<RowMatrix4f> out { frameModelMatrices.data() + (1+i) * 16};
			// out = (vp * model).cast<float>();
			out = (model).cast<float>();
		}
		void* dbuf = (void*) matrices.mem.mapMemory(0, (1+nInSet)*16*4, {});
		memcpy(dbuf, frameModelMatrices.data(), (1+nInSet)*16*4);
		matrices.mem.unmapMemory();
	}

	// Render instanced. But cannot use instancing in correct way (one draw call). Must make several
	// calls due to how I setup the attributes. Not going to change now though.
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*verts.buffer}, {0u});
		cmd.bindIndexBuffer(*inds.buffer, 0, vk::IndexType::eUint16);
		// cmd.drawIndexed(nInds, nInSet, 0,0,0);
		for (int i=0; i<nInSet; i++)
			cmd.drawIndexed(nInds, 1, 0,i*10,i);
	}

}
