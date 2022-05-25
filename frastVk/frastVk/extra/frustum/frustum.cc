#include "frastVk/utils/eigen.h"

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

			8,9,
			8,10,
			8,11,
			8,12,
			8,13
		};
		nInds = inds_.size();

		verts.setAsVertexBuffer(14*nInSet*7 * 4, true, vk::BufferUsageFlagBits::eTransferDst);
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

	// Make path stuff
	/*
		ResidentBuffer paths;
		int maxPaths = 8;
		int maxPathLen;
		std::vector<int> idToCurrentPath;
		std::vector<Vector4f> pathColors;
	*/
	paths.setAsVertexBuffer(maxPaths*maxPathLen*4*3, true);
	paths.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
	idToCurrentPath.resize(nInSet, -1);
	pathLens.resize(maxPaths, 0);
	pathColors.resize(maxPaths);

	// Pipeline
	{
		pathPipelineStuff.setup_viewport(app->windowWidth, app->windowHeight);
		loadShader(app->deviceGpu, pathPipelineStuff.vs, pathPipelineStuff.fs, "frustum/path");

		pathPipelineStuff.setLayouts.push_back(*globalDescSetLayout);

		vk::VertexInputAttributeDescription posAttr;
		posAttr.binding = 0;
		posAttr.location = 0;
		posAttr.offset = 0;
		posAttr.format = vk::Format::eR32G32B32Sfloat;
		vk::VertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = 3*4;
		mainBinding.inputRate = vk::VertexInputRate::eVertex;
		VertexInputDescription vertexDesc;
		vertexDesc.attributes = { posAttr};
		vertexDesc.bindings = { mainBinding };

		pathPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eFragment,
				0,
				4*4 });

		PipelineBuilder builder;
		builder.init(
				vertexDesc,
				vk::PrimitiveTopology::eLineStrip,
				*pathPipelineStuff.vs, *pathPipelineStuff.fs);

		pathPipelineStuff.build(builder, app->deviceGpu, *app->simpleRenderPass.pass, app->mainSubpass());
	}
}

void FrustumSet::setColor(int n, const float color[4]) {
	Eigen::Vector4f c { color[0], color[1], color[2], color[3] };
	void* dbuf = (void*) verts.mem.mapMemory(14*4*7*n, 14*4*7, {});
	Eigen::Map<RowMatrix<float,14,7>> vs { (float*) dbuf };
	vs.topRightCorner<8,4>().rowwise() = c.transpose();
	Eigen::Vector4f c2 { color[0], color[1], color[2], color[3] * .2f };
	vs.block<1,4>(8,3).rowwise() = c2.transpose();
	Eigen::Vector4f c3 { color[0], color[1], color[2], color[3] * .01f };
	vs.bottomRightCorner<5,4>().rowwise() = c3.transpose();
	verts.mem.unmapMemory();

	pathColors[n] = c;
}

void FrustumSet::setIntrin(int n, float w, float h, float fx, float fy) {
	void* dbuf = (void*) verts.mem.mapMemory(14*4*7*n, 14*4*7, {});
	float z = -1.f, o = 1.f;
	// const float ray_far = 19000.0 / 6378137.0;
	Eigen::Map<RowMatrix4d> model { modelMatrices.data() + n * 16 };
	float ray_far = model.topRightCorner<3,1>().norm() - .9985;
	ray_far = std::min(std::max(ray_far, 100.f / 6378137.0f), 100000.f / 6378137.0f);
	RowMatrix<float,14,3> new_vs; new_vs <<
			z,z,near,
			o,z,near,
			o,o,near,
			z,o,near,

			z,z,far,
			o,z,far,
			o,o,far,
			z,o,far,

			0,0,0,
			0,0, ray_far,
			z,z, ray_far,
			o,z, ray_far,
			o,o, ray_far,
			z,o, ray_far;
	new_vs.col(0) *= .5 * (w/fx);
	new_vs.col(1) *= .5 * (h/fy);
	new_vs.block<4,2>(0,0) *= near;
	new_vs.block<4,2>(4,0) *= far;
	new_vs.block<4,2>(10,0) *= ray_far;
	Eigen::Map<RowMatrix<float,14,7>> vs { (float*) dbuf };
	vs.leftCols(3) = new_vs;
	verts.mem.unmapMemory();
}

void FrustumSet::setNextPath(int n, const Vector4f& color) {
	// Find next unoccupied path slot
	for (int j=pathIdx; j<pathIdx+maxPaths; j++) {
		int jj = j % maxPaths;
		bool okay = true;
		for (int i=0; i<nInSet; i++) {
			if (idToCurrentPath[i] == jj) {
				okay = false;
			}
		}
		if (okay) {
			pathIdx = jj;
			break;
		}
	}
	idToCurrentPath[n] = pathIdx;
	if (pathIdx >= maxPaths) pathIdx = 0;
	pathLens[pathIdx] = 0;

	pathColors[pathIdx] = color;
}
void FrustumSet::setPose(int n, const Eigen::Vector3d& pos, const RowMatrix3d& R, bool pushPath) {

	if (pushPath) {
		if (idToCurrentPath[n] == -1) {
			// Must be first push
			idToCurrentPath[n] = pathIdx;
			pathLens[pathIdx] = 0;
			pathIdx++;
			if (pathIdx >= maxPaths) pathIdx = 0;
		}

		int pid = idToCurrentPath[n];
		int pi = pathLens[pid];
		// simple case
		if (pi < maxPathLen) {
			float* dbuf = (float*) paths.mem.mapMemory(4*3*(maxPathLen*pid + pi), 4*3, {});
			dbuf[0] = (float)pos(0);
			dbuf[1] = (float)pos(1);
			dbuf[2] = (float)pos(2);
			paths.mem.unmapMemory();
			pathLens[pid]++;
		} else {
			// Must decimate

			float* dbuf = (float*) paths.mem.mapMemory(4*3*(maxPathLen*pid), 4*3*maxPathLen, {});
			for (int i=0; i<maxPathLen/2; i++) {
				dbuf[i*3+0] = dbuf[i*2*3+0];
				dbuf[i*3+1] = dbuf[i*2*3+1];
				dbuf[i*3+2] = dbuf[i*2*3+2];
			}
			int pi = maxPathLen / 2;
			pathLens[pid] = maxPathLen / 2 + 1;
			dbuf[pi*3+0] = (float)pos(0);
			dbuf[pi*3+1] = (float)pos(1);
			dbuf[pi*3+2] = (float)pos(2);
			paths.mem.unmapMemory();
		}
	}


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
			cmd.drawIndexed(nInds, 1, 0,i*14,i);
	}

	// Render paths
	{
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pathPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pathPipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);

		cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*paths.buffer}, {0u});

		for (int pid=0; pid<maxPaths; pid++) {
			float* p_color = pathColors[pid].data();


			if (pathLens[pid] > 0) {
				// fmt::print(" - pid {} rendering {} (color {} {} {} {})\n", pid, pathLens[pid], p_color[0], p_color[1], p_color[2], p_color[3]);
				cmd.pushConstants(*pathPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const float>{4u, p_color});
				int vOff = pid * maxPathLen;
				cmd.draw(pathLens[pid], 1, vOff,0);
			}
		}

	}

}
