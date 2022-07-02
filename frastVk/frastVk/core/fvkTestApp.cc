#include "fvkApi.h"
#include "fvkShaders.h"
#include "imgui_app.h"
#include <fmt/color.h>
#include <cmath>

// using BaseClass = BaseApp;
using BaseClass = ImguiApp;

struct TestApp_FixedTriangle : public BaseClass {
	int i = 0;
	using BaseClass::BaseClass;
	GraphicsPipeline triPipeline;

	inline virtual void initVk() {
		BaseClass::initVk();

		CameraSpec spec { 512,512, 45. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);

		assert(not loadShader(mainDevice,
				triPipeline.vs,
				triPipeline.fs,
				"simple/tri"));

		float viewportXYWH[4] = {
			0,0,
			(float)windowWidth,
			(float)windowHeight
		};
		PipelineBuilder builder;
		builder.depthTest = cfg.depth;

		VertexInputDescription vid;
		/*
		VkVertexInputAttributeDescription positionAttribute;
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = 0;
		vid.attributes.push_back(positionAttribute);
		VkVertexInputBindingDescription binding_ = { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX };
		vid.bindings.push_back(binding_);
		*/

		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, triPipeline.vs, triPipeline.fs);
		triPipeline.create(mainDevice, viewportXYWH, builder, simpleRenderPass, 0);
		fmt::print(fmt::fg(fmt::color::green), " - Created simple/tri pipleline!\n");
	}

	inline virtual void doRender(RenderState& rs) override {
		auto& fd = *rs.frameData;

		fd.cmd.begin();
		fd.cmd.clearImage(*fd.swapchainImg, {(float)(i%3==0), (float)(i%3==1), (float)(i%3==2), 1.f});
		simpleRenderPass.begin(fd.cmd, fd);
		vkCmdBindPipeline(fd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline);
		vkCmdDraw(fd.cmd, 3, 1, 0, 0);
		simpleRenderPass.end(fd.cmd, fd);
		i++;
	}
};

struct TestApp_Shape : public BaseClass {
	int i = 0;
	using BaseClass::BaseClass;
	GraphicsPipeline triPipeline;
	
	ExBuffer vertexBuffer;
	ExBuffer indexBuffer;
	DescriptorSet dset;
	uint32_t numInds = 0;

	ExBuffer globalDataBuffer;
	float* mat44_map = nullptr;

	inline virtual void initVk() {
		BaseClass::initVk();

		CameraSpec spec { 512,512, 45. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);

		assert(not loadShader(mainDevice,
				triPipeline.vs,
				triPipeline.fs,
				"simple/shape"));

		// Setup vertex and index buffers

		float verts[] = {
			.5, -.5, .1,
			-.5, -.5, .1,
			.0,  .7, .1 };
		numInds = 3;
		if (0) {
				vertexBuffer.set(3*3*4,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
				vertexBuffer.create(mainDevice);
				float* vtx = (float*) vertexBuffer.map(0, 3*3*4);
				memcpy(vtx, verts, sizeof(verts));
				vertexBuffer.unmap();
		} else {
				vertexBuffer.set(3*3*4, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
				vertexBuffer.create(mainDevice);

				ExUploader uploader;
				uploader.create(&mainDevice, &mainQueue);
				uploader.enqueueUpload(vertexBuffer, verts, sizeof(verts), 0);
				fmt::print(fmt::fg(fmt::color::magenta), " - uploading\n");
				uploader.execute();
				fmt::print(fmt::fg(fmt::color::magenta), " - done uploading\n");
		}

		indexBuffer.set(3*sizeof(uint16_t),
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
		indexBuffer.create(mainDevice);
		uint16_t* ind = (uint16_t*) indexBuffer.map(0, 3*2);
		for (int i=0; i<numInds; i++) ind[i] = static_cast<uint16_t>(i);
		indexBuffer.unmap();

		// Setup UBO and descriptors

		globalDataBuffer.set(16*4,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		globalDataBuffer.create(mainDevice);

		uint32_t globalDataBindingIdx = dset.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		dset.create(dpool, {&triPipeline});

		mat44_map = (float*) globalDataBuffer.map();
		for (int i=0; i<16; i++) mat44_map[i] = (i%5) == 0;

		VkDescriptorBufferInfo bufferInfo { globalDataBuffer, 0, 4*16 };
		dset.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				dset, globalDataBindingIdx,
				0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				nullptr, &bufferInfo, nullptr
				});


		// Setup pipeline

		float viewportXYWH[4] = {
			0,0,
			(float)windowWidth,
			(float)windowHeight
		};
		PipelineBuilder builder;
		builder.depthTest = cfg.depth;

		VertexInputDescription vid;
		VkVertexInputAttributeDescription positionAttribute;
		positionAttribute.binding = 0;
		positionAttribute.location = 0;
		positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		positionAttribute.offset = 0;
		vid.attributes.push_back(positionAttribute);
		VkVertexInputBindingDescription binding_ = { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX };
		vid.bindings.push_back(binding_);

		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, triPipeline.vs, triPipeline.fs);
		triPipeline.create(mainDevice, viewportXYWH, builder, simpleRenderPass, 0);
		fmt::print(fmt::fg(fmt::color::green), " - Created shape pipleline!\n");
	}

	inline virtual void doRender(RenderState& rs) override {
		auto& fd = *rs.frameData;

		for (int i=0; i<16; i++) mat44_map[i] = (i%5) == 0;
		float t = i / 100.f;
		mat44_map[0*4+0] = std::cos(t);
		mat44_map[1*4+0] = std::sin(t);
		mat44_map[0*4+1] = -std::sin(t);
		mat44_map[1*4+1] = std::cos(t);

		fd.cmd.begin();
		fd.cmd.clearImage(*fd.swapchainImg, {(float)(i%3==0), (float)(i%3==1), (float)(i%3==2), 1.f});
		simpleRenderPass.begin(fd.cmd, fd);
		vkCmdBindPipeline(fd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline);
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(fd.cmd, 0, 1, &vertexBuffer.buf, &offset);
		vkCmdBindIndexBuffer(fd.cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdBindDescriptorSets(fd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipeline.layout, 0, 1, &dset.dset, 0, 0);
		vkCmdDrawIndexed(fd.cmd, numInds, 1, 0, 0, 0);
		simpleRenderPass.end(fd.cmd, fd);
		i++;
	}
};

int main() {
	AppConfig cfg;

	// TestApp_FixedTriangle app(cfg);
	TestApp_Shape app(cfg);
	app.initVk();

	for (int i=0; i<200; i++) {

		app.render();

		usleep(20'000);
	}

	return 0;
}
