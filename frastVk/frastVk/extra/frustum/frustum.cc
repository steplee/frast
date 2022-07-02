#include "frastVk/utils/eigen.h"

#include "frustum.h"
#include <fmt/color.h>

#include "frastVk/core/fvkShaders.h"


FrustumSet::FrustumSet(BaseApp* app, int nInSet) : app(app), nInSet(nInSet) {
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

		verts.set(14*nInSet*7*4,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		inds.set(inds_.size()*2, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		verts.create(app->mainDevice);
		inds.create(app->mainDevice);

		app->generalUploader.enqueueUpload(inds, inds_.data(), inds_.size() * 2, 0);
		app->generalUploader.execute();

		globalBuffer.set((1+nInSet) * 16 * 4,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		globalBuffer.create(app->mainDevice);
		globalBuffer.map();
		modelMatrices.resize(16*nInSet, 0);

		for (int i=0; i<nInSet; i++)
			setIntrin(i, 1,1, 1,1);
	}

	{
		loadShader(app->mainDevice, frustumPipeline.vs, frustumPipeline.fs, "frustum/frustum");

		// frustumPipeline.setLayouts.push_back(globalDescSetLayout);
		// GlobalData
		uint32_t globalDataBindingIdx = globalDescSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		globalDescSet.create(app->dpool, {&frustumPipeline, &pathPipeline});

		VertexInputDescription vid;
		VkVertexInputAttributeDescription attrPos { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
		VkVertexInputAttributeDescription attrCol { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8 };
		vid.attributes = { attrPos, attrCol };
		vid.bindings = { VkVertexInputBindingDescription { 0, 7*4, VK_VERTEX_INPUT_RATE_VERTEX } };

		float viewportXYWH[4] = {
			0,0,
			(float)app->windowWidth,
			(float)app->windowHeight
		};
		PipelineBuilder builder;
		builder.depthTest = true;
		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, frustumPipeline.vs, frustumPipeline.fs);

		frustumPipeline.create(app->mainDevice, viewportXYWH, builder, app->simpleRenderPass.pass, 0);

		// Write descriptor
		VkDescriptorBufferInfo bufferInfo { globalBuffer, 0, sizeof(GlobalBuffer) };
		globalDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				globalDescSet, 0,
				0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				nullptr, &bufferInfo, nullptr
				});
	}

	idToCurrentPath.resize(nInSet, -1);
	pathLens.resize(maxPaths, 0);
	pathColors.resize(maxPaths);
	paths.set(maxPaths*maxPathLen*4*3,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	paths.create(app->mainDevice);
	// Make path stuff
	/*
	paths.setAsVertexBuffer(maxPaths*maxPathLen*4*3, true);
	paths.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);

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
	*/

	{
		loadShader(app->mainDevice, pathPipeline.vs, pathPipeline.fs, "frustum/path");

		// GlobalData (already created and tracked above)
		// uint32_t globalDataBindingIdx = globalDescSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		// globalDescSet.create(app->dpool, {&frustumPipeline, &pathPipeline});

		VertexInputDescription vid;
		VkVertexInputAttributeDescription attrPos { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
		vid.attributes = { attrPos };
		vid.bindings = { VkVertexInputBindingDescription { 0, 3*4, VK_VERTEX_INPUT_RATE_VERTEX } };

		float viewportXYWH[4] = {
			0,0,
			(float)app->windowWidth,
			(float)app->windowHeight
		};
		PipelineBuilder builder;
		builder.depthTest = true;
		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, pathPipeline.vs, pathPipeline.fs);

		pathPipeline.pushConstants.push_back(VkPushConstantRange{
				VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				4*4 });

		pathPipeline.create(app->mainDevice, viewportXYWH, builder, app->simpleRenderPass.pass, 0);
	}

}

void FrustumSet::setColor(int n, const float color[4]) {
	Eigen::Vector4f c { color[0], color[1], color[2], color[3] };
	Eigen::Vector4f c2 { color[0], color[1], color[2], color[3] * .2f };
	Eigen::Vector4f c3 { color[0], color[1], color[2], color[3] * .01f };

	void* dbuf = (void*) verts.map(14*4*7*n, 14*4*7);
	Eigen::Map<RowMatrix<float,14,7>> vs { (float*) dbuf };

	vs.topRightCorner<8,4>().rowwise() = c.transpose();
	vs.block<1,4>(8,3).rowwise() = c2.transpose();
	vs.bottomRightCorner<5,4>().rowwise() = c3.transpose();
	verts.unmap();

	pathColors[n] = c;
}

void FrustumSet::setIntrin(int n, float w, float h, float fx, float fy) {
	void* dbuf = (void*) verts.map(14*4*7*n, 14*4*7);
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
	verts.unmap();
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
			float* dbuf = (float*) paths.map(4*3*(maxPathLen*pid + pi), 4*3);
			dbuf[0] = (float)pos(0);
			dbuf[1] = (float)pos(1);
			dbuf[2] = (float)pos(2);
			paths.unmap();
			pathLens[pid]++;
		} else {
			// Must decimate

			float* dbuf = (float*) paths.map(4*3*(maxPathLen*pid), 4*3*maxPathLen);
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
			paths.unmap();
		}
	}


	Eigen::Map<RowMatrix4d> model { modelMatrices.data() + n * 16 };
	model.topRightCorner<3,1>() = pos;
	model.topLeftCorner<3,3>() = R;
	model.row(3) << 0,0,0,1;

}




void FrustumSet::render(RenderState& rs, Command& cmd) {

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
		// void* dbuf = (void*) globalBuffer.map(0, (1+nInSet)*16*4);
		void* dbuf = (void*) globalBuffer.mappedAddr;
		memcpy(dbuf, frameModelMatrices.data(), (1+nInSet)*16*4);
		// globalBuffer.unmap();
	}

	// Render instanced. But cannot use instancing in correct way (one draw call). Must make several
	// calls due to how I setup the attributes. Not going to change now though.
	{
		/*
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*verts.buffer}, {0u});
		cmd.bindIndexBuffer(*inds.buffer, 0, vk::IndexType::eUint16);
		// cmd.drawIndexed(nInds, nInSet, 0,0,0);
		for (int i=0; i<nInSet; i++)
			cmd.drawIndexed(nInds, 1, 0,i*14,i);
		*/

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frustumPipeline.layout, 0, 1, &globalDescSet.dset, 0, 0);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, frustumPipeline);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &verts.buf, &offset);
		vkCmdBindIndexBuffer(cmd, inds.buf, 0, VK_INDEX_TYPE_UINT16);

		for (int i=0; i<nInSet; i++)
			vkCmdDrawIndexed(cmd, nInds, 1, 0, 0, i);
	}

	/*
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
	*/

	if(1){
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pathPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pathPipeline.layout, 0, 1, &globalDescSet.dset, 0, 0);

		// cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*paths.buffer}, {0u});
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &paths.buf, &offset);

		for (int pid=0; pid<maxPaths; pid++) {
			float* p_color = pathColors[pid].data();

			if (pathLens[pid] > 0) {
				
				vkCmdPushConstants(cmd, pathPipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4*4, (void*)p_color);
				int vOff = pid * maxPathLen;
				//cmd.draw(pathLens[pid], 1, vOff,0);
				vkCmdDraw(cmd, pathLens[pid], 1, vOff, 0);
			}
		}

	}

}
