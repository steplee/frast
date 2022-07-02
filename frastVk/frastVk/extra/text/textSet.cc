#include "textSet.h"
#include "font.hpp"
#include "frastVk/core/fvkShaders.h"

SimpleTextSet::~SimpleTextSet() {
	sampler.destroy(app->mainDevice);
}

void SimpleTextSet::render(RenderState& rs, Command &cmd) {
	/*
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*descSet}, nullptr);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	for (int i=0; i<maxStrings; i++)
		if (stringLengths[i] > 0)
			cmd.draw(6*stringLengths[i], 1, 0, i);
	*/

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &descSet.dset, 0, 0);
	for (int i=0; i<maxStrings; i++)
		if (stringLengths[i] > 0)
			vkCmdDraw(cmd, 6*stringLengths[i], 1, 0, i);

}

SimpleTextSet::SimpleTextSet(BaseApp* app_) {
	ww = app_->windowWidth;
	hh = app_->windowHeight;
	app = app_;


	// void set(VkExtent2D extent, VkFormat fmt, VkMemoryPropertyFlags memFlags, VkImageUsageFlags usageFlags, VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT);
	// void create(Device& device, VkImageLayout initialLayout=VK_IMAGE_LAYOUT_UNDEFINED);

	// Create buffers
	ubo.set(sizeof(TextBufferData)*maxStrings,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	ubo.create(app->mainDevice);
	ubo.map();


	// Load texture
	uint32_t full_width  = _texWidth * _cols;
	uint32_t full_height = _texHeight * _rows;
	fontTex.set(VkExtent2D{full_width,full_height}, VK_FORMAT_R8_UNORM,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	fontTex.create(app->mainDevice);

	app->generalUploader.enqueueUpload(fontTex, (uint8_t*)_image, full_height*full_width, 0, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
	app->generalUploader.execute();

	uint32_t a = descSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
	uint32_t b = descSet.addBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT);
	descSet.create(app->dpool, {&pipeline});

	// Create sampler

	sampler.create(app->mainDevice, VkSamplerCreateInfo{
			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			nullptr, 0,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			VK_SAMPLER_MIPMAP_MODE_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,

			0.f,
			VK_FALSE, 0.f,
			VK_FALSE, VK_COMPARE_OP_NEVER,
			0, 0,
			VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			true
	});


	VkDescriptorBufferInfo bufferInfo { ubo, 0, sizeof(TextBufferData) };
	descSet.update(VkWriteDescriptorSet{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
			descSet, 0,
			0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			nullptr, &bufferInfo, nullptr
			});

	VkDescriptorImageInfo imgInfo { sampler, fontTex.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL };
	descSet.update(VkWriteDescriptorSet{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
			descSet, 1,
			0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			&imgInfo, 0, nullptr
			});



	// Setup pipeline
	{
		loadShader(app->mainDevice, pipeline.vs, pipeline.fs, "extra/textSet");

		VertexInputDescription vid;
		// VkVertexInputAttributeDescription attrPos       { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
		// VkVertexInputAttributeDescription attrIntensity { 1, 0, VK_FORMAT_R32_SFLOAT, 3*4 };
		// vid.attributes = { attrPos, attrIntensity };
		// vid.bindings = { VkVertexInputBindingDescription { 0, 4*4, VK_VERTEX_INPUT_RATE_VERTEX } };

		PipelineBuilder builder;
		builder.depthTest = false;
		builder.depthWrite = false;
		builder.additiveBlending = true;
		// builder.replaceBlending = true;
		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pipeline.vs, pipeline.fs);


		float viewportXYWH[4] = {
			0,0,
			(float)app->windowWidth,
			(float)app->windowHeight
		};
		pipeline.create(app->mainDevice, viewportXYWH, builder, app->simpleRenderPass, 0);
	}
	
}

void SimpleTextSet::reset() {
	for (int i=0; i<maxStrings; i++) stringLengths[i] = 0;
}

void SimpleTextSet::setText(int i, const std::string& text, const float matrix[16], const float color[4]) {
	stringLengths[i] = text.length();

	// TextBufferData* tbd = (TextBufferData*) ubo.mem.mapMemory(sizeof(TextBufferHeader)+sizeof(TextBufferData)*i, sizeof(TextBufferData), {});
	TextBufferData* tbd = (TextBufferData*)( static_cast<char*>(ubo.mappedAddr)+sizeof(TextBufferHeader)+sizeof(TextBufferData)*i );
	memcpy(tbd->matrix, matrix, 4*16);
	for (int j=0; j<4; j++) tbd->color[j] = color[j];
	for (int j=0; j<stringLengths[i]; j++)
		tbd->chars[j] = _charIndices[text[j]];
	// ubo.mem.unmapMemory();

}

void SimpleTextSet::setAreaAndSize(float offx, float offy, float ww, float hh, float scale, const float mvp[16]) {

	// TextBufferHeader* tbh = (TextBufferHeader*) ubo.mem.mapMemory(0, sizeof(TextBufferHeader), {});
	/*
	tbh->offset[0] = offx;
	tbh->offset[1] = offy;
	tbh->windowSize[0] = ww;
	tbh->windowSize[1] = hh;
	*/
	// tbh->size[0] = scale;
	// tbh->size[1] = scale;
	// memcpy(tbh->matrix, mvp, 4*16);
	// ubo.mem.unmapMemory();

	
	TextBufferHeader* tbh = (TextBufferHeader*) ubo.mappedAddr;
	memcpy(tbh->matrix, mvp, 4*16);
}
