#include "textSet.h"
#include "font.hpp"
#include "frastVk/core/load_shader.hpp"

void SimpleTextSet::render(RenderState& rs, vk::CommandBuffer &cmd) {
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipelineLayout, 0, {1,&*descSet}, nullptr);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineStuff.pipeline);
	for (int i=0; i<maxStrings; i++)
		if (stringLengths[i] > 0)
			cmd.draw(6*stringLengths[i], 1, 0, i);
}

SimpleTextSet::SimpleTextSet(BaseVkApp* app_) {
	ww = app_->windowWidth;
	hh = app_->windowHeight;
	app = app_;



	// Load texture
	int full_width  = _texWidth * _cols;
	int full_height = _texHeight * _rows;
	fontTex.unnormalizedCoordinates = true;
	fontTex.createAsTexture(app->uploader, full_height, full_width, vk::Format::eR8Unorm, (uint8_t*)_image);
	// fontTex.createAsTexture(app->uploader, full_width, full_height, vk::Format::eR8Unorm, (uint8_t*)_image);

	std::vector<vk::DescriptorPoolSize> poolSizes = {
		vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 1 },
		vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 2 },
	};
	vk::DescriptorPoolCreateInfo poolInfo {
		vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
			1,
			(uint32_t)poolSizes.size(), poolSizes.data()
	};
	descPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

	std::vector<vk::DescriptorSetLayoutBinding> bindings;
	bindings.push_back({ 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment });
	bindings.push_back({ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment });

	vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
	descSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

	vk::DescriptorSetAllocateInfo allocInfo {
		*descPool, 1, &*descSetLayout
	};
	descSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

	// Create buffers
	ubo.setAsUniformBuffer(sizeof(TextBufferData)*maxStrings, true);
	ubo.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);

	// Bind it
	vk::DescriptorImageInfo i_info = vk::DescriptorImageInfo{
			*fontTex.sampler,
			*fontTex.view,
			vk::ImageLayout::eShaderReadOnlyOptimal
	};
	vk::DescriptorBufferInfo b_info {
		*ubo.buffer, 0, VK_WHOLE_SIZE
	};
	std::vector<vk::WriteDescriptorSet> writeDesc = {
		{
			*descSet,
			0, 0, 1u,
			vk::DescriptorType::eUniformBuffer,
			nullptr,
			&b_info,
			nullptr
		},
		{
		 *descSet,
		 1, 0, 1u,
		 vk::DescriptorType::eCombinedImageSampler,
		 &i_info,
		 nullptr,
		 nullptr
		 }
	};
	app->deviceGpu.updateDescriptorSets({(uint32_t)writeDesc.size(), writeDesc.data()}, nullptr);

	{
		// Setup pipeline
		pipelineStuff.setup_viewport(ww, hh);
		loadShader(app->deviceGpu, pipelineStuff.vs, pipelineStuff.fs, "extra/textSet");

		pipelineStuff.setLayouts.push_back(*descSetLayout);

		/*
		pipelineStuff.pushConstants.push_back(vk::PushConstantRange{
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				0,
				sizeof(ParticleCloudPushConstants) });
		*/

		PipelineBuilder builder;
		builder.additiveBlending = false;
		builder.depthTest = false;
		builder.depthWrite = false;
		builder.init(
				{},
				vk::PrimitiveTopology::eTriangleList,
				*pipelineStuff.vs, *pipelineStuff.fs);
		pipelineStuff.build(builder, app->deviceGpu, *app->simpleRenderPass.pass, app->mainSubpass());
	}
	
}

void SimpleTextSet::reset() {
	for (int i=0; i<maxStrings; i++) stringLengths[i] = 0;
}

void SimpleTextSet::setText(int i, const std::string& text, const float matrix[16]) {
	stringLengths[i] = text.length();

	TextBufferData* tbd = (TextBufferData*) ubo.mem.mapMemory(sizeof(TextBufferData)*i, sizeof(TextBufferData), {});
	memcpy(tbd->matrix, matrix, 4*16);
	for (int j=0; j<stringLengths[i]; j++)
		tbd->chars[j] = _charIndices[text[j]];
	ubo.mem.unmapMemory();

}
