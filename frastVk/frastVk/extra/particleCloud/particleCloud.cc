#include "particleCloud.h"
#include "frastVk/core/fvkShaders.h"

#include <fmt/core.h>
#include <fmt/color.h>

// #define USE_TIMER
// #include <chrono>
// #include <frast/utils/timer.hpp>
// static AtomicTimer timer_pc("pc::render");

#define dprint(...) fmt::print(fmt::fg(fmt::color::medium_slate_blue), __VA_ARGS__);
//#define dprint(...) {}


ParticleCloudRenderer::ParticleCloudRenderer(BaseApp* app, int capacity) : app(app) {
	setup(capacity);
	setup_fbos();
	setup_pipelines();
}

void ParticleCloudRenderer::setup(int capacity) {
	w = app->windowWidth;
	h = app->windowHeight;

	setup_buffers(capacity);
}

void ParticleCloudRenderer::setup_particle_buffer(int capacity_) {
	capacity = capacity_;
	particleBuffer.deallocate();
	particleBuffer.set(capacity*4*4, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	particleBuffer.create(app->mainDevice);
}
void ParticleCloudRenderer::setup_buffers(int capacity_) {
	setup_particle_buffer(capacity_);

	globalBuffer.deallocate();
	globalBuffer.set(4*4*4,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	globalBuffer.create(app->mainDevice);
	globalBuffer.map();
}

void ParticleCloudRenderer::setup_fbos() {

	/////////////////////////////////////////////////
	// Setup passes
	/////////////////////////////////////////////////

	// Make sure we do not clear the attachments
	outputPass.description = app->simpleRenderPass.description;
	outputPass.description.attDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	outputPass.description.attDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	outputPass.description.attDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	outputPass.description.attDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	outputPass.description.attDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	outputPass.description.attDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Note: output pass has NO framebuffers, because it uses the one from the app that it is passed
	outputPass.clearCount = 0;
	auto rpInfo = outputPass.description.makeCreateInfo();
	assertCallVk(vkCreateRenderPass(app->mainDevice, &rpInfo, nullptr, &outputPass.pass));
	outputPass.framebufferWidth = app->simpleRenderPass.framebufferWidth;
	outputPass.framebufferHeight = app->simpleRenderPass.framebufferHeight;
	outputPass.device = app->mainDevice;
	outputPass.inputLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	outputPass.outputLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	
	/*
	dprint(" - setting up fbos\n");
	// Multiply by four again, because it's actually float data
	std::vector<uint8_t> emptyImage(h*w*4*4, 0);

	// 0) Setup output pass.
	{
		vk::AttachmentDescription colorAttachment {
			{}, app->scSurfaceFormat.format,
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				// vk::ImageLayout::eGeneral,
				// vk::ImageLayout::eUndefined,
				// vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eColorAttachmentOptimal
				// vk::ImageLayout::ePresentSrcKHR
		};
		vk::AttachmentReference colorAttachmentRef { 0, vk::ImageLayout::eColorAttachmentOptimal };

		vk::AttachmentDescription depthAttachment {
			{}, vk::Format { app->simpleRenderPass.depthImages[0].format },
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eDontCare, // NOTE: Don't write to depth, only read!
				vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::ImageLayout::eDepthStencilAttachmentOptimal };
		vk::AttachmentReference depthAttachmentRef { 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };

		std::vector<vk::SubpassDescription> subpasses {
			// { {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef },
			{ {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef },
		};
		vk::SubpassDependency depthDependency { VK_SUBPASS_EXTERNAL, 0,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
				{}, vk::AccessFlagBits::eDepthStencilAttachmentRead, {} };
		std::vector<vk::SubpassDependency> dependencies = {
		// Depth
		{ VK_SUBPASS_EXTERNAL, 0,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			{}, vk::AccessFlagBits::eDepthStencilAttachmentWrite, {} },
	};

		vk::AttachmentDescription atts[2] = { colorAttachment, depthAttachment };
		vk::RenderPassCreateInfo rpInfo {
			{},
				{ 2, atts },
				subpasses,
				dependencies
				//{}
		};
		dprint("\t - create output pass\n");
		outputPass = std::move(app->deviceGpu.createRenderPass(rpInfo));
	}

	// 1) Setup particle pass.
	{
		// Color
		vk::AttachmentDescription colorAttachment {
			{},
				// app->scSurfaceFormat.format,
				// vk::Format::eR32Sfloat,
				vk::Format::eR32G32B32A32Sfloat,
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
				// vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eColorAttachmentOptimal};
				// vk::ImageLayout::ePresentSrcKHR };

		vk::AttachmentReference colorAttachmentRef {
			0,
				vk::ImageLayout::eColorAttachmentOptimal
		};

		// Depth
		vk::AttachmentDescription depthAttachment {
			{},
				vk::Format { app->simpleRenderPass.depthImages[0].format },
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eLoad,
				// vk::AttachmentStoreOp::eStore,
				vk::AttachmentStoreOp::eDontCare, // NOTE: Don't write to depth, only read!
				vk::AttachmentLoadOp::eDontCare,
				vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				//vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eDepthStencilAttachmentOptimal };

		vk::AttachmentReference depthAttachmentRef {
			1,
				vk::ImageLayout::eDepthStencilAttachmentOptimal
		};

		vk::SubpassDescription subpass0 {
			{},
				vk::PipelineBindPoint::eGraphics,
				{ },
				{ 1, &colorAttachmentRef },
				{ },
				&depthAttachmentRef
		};


		vk::SubpassDependency depthDependency {
			VK_SUBPASS_EXTERNAL, 0,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
				{},
				vk::AccessFlagBits::eDepthStencilAttachmentWrite,
				{}
		};


		vk::AttachmentDescription atts[2] = { colorAttachment, depthAttachment };
		vk::RenderPassCreateInfo rpInfo {
			{},
				{ 2, atts },
				{ 1, &subpass0 },
				{ 1, &depthDependency }
		};
		dprint("\t - create particle pass\n");
		particlePass = std::move(app->deviceGpu.createRenderPass(rpInfo));

		dprint("\t - create particle images ({} {})\n", h,w);
		for (int i=0; i<app->scNumImages; i++) {
			particleImages.push_back(ResidentImage{});
			// particleImages.back().unnormalizedCoordinates = true;
			particleImages.back().createAsTexture(app->uploader, h, w,
					// app->scSurfaceFormat.format,
					// vk::Format::eR32Sfloat,
					vk::Format::eR32G32B32A32Sfloat,
					// vk::Format::eR32Sfloat,
					emptyImage.data(),
					vk::ImageUsageFlagBits::eColorAttachment |
					vk::ImageUsageFlagBits::eSampled,
					vk::SamplerAddressMode::eClampToBorder
					);

			vk::ImageView views[2] = { *particleImages[i].view, *app->simpleRenderPass.depthImages[i].view };

			vk::FramebufferCreateInfo fbInfo {
				{},
					*particlePass,
					{ 2, views},
					w, h,
					1
			};
			particleFramebuffers.push_back(std::move(app->deviceGpu.createFramebuffer(fbInfo)));
		}
	}

	// 2) Create ping-pong pass with 2*N fbos
	{
		// Color
		vk::AttachmentDescription colorAttachment {
			{},
				// app->scSurfaceFormat.format,
				// vk::Format::eR32Sfloat,
				vk::Format::eR32G32B32A32Sfloat,
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
				// vk::AttachmentLoadOp::eNoneEXT, vk::AttachmentStoreOp::eStore,
				// vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				// vk::ImageLayout::eShaderReadOnlyOptimal,
				// vk::ImageLayout::eShaderReadOnlyOptimal };
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eColorAttachmentOptimal };
				// vk::ImageLayout::ePresentSrcKHR };

		vk::AttachmentReference colorAttachmentRef {
			0,
				vk::ImageLayout::eColorAttachmentOptimal
		};

		vk::SubpassDescription subpass0 {
			{},
				vk::PipelineBindPoint::eGraphics,
				{ },
				{ 1, &colorAttachmentRef },
				{ },
				{ }
		};


		vk::AttachmentDescription atts[1] = { colorAttachment };
		vk::RenderPassCreateInfo rpInfo {
			{},
				{ 1, atts },
				{ 1, &subpass0 },
				{ }
		};
		filterPass = std::move(app->deviceGpu.createRenderPass(rpInfo));


		for (int jj=0; jj<2; jj++) {
			// Create images & views

			dprint("\t - create filterImages[{}]\n", jj);
			// filterImages[jj].unnormalizedCoordinates = true;
			filterImages[jj].createAsTexture(app->uploader, h, w,
					// app->scSurfaceFormat.format, emptyImage.data(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
					// vk::Format::eR32Sfloat, emptyImage.data(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
					vk::Format::eR32G32B32A32Sfloat, emptyImage.data(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
					vk::SamplerAddressMode::eClampToBorder);

			dprint("\t - create filterFramebuffers[{}]\n", jj);
			vk::ImageView views[1] = { *filterImages[jj].view };
			vk::FramebufferCreateInfo fbInfo {
				{},
					*filterPass,
					{ 1, views},
					w, h,
					1
			};
			filterFramebuffers[jj] = std::move(app->deviceGpu.createFramebuffer(fbInfo));
		}
	}
	*/

}

void ParticleCloudRenderer::setup_pipelines() {
	dprint(" - [ParticleCloudRenderer] setup pipelines\n");

	// Create global buffer
	{
		// particlePipeline.setLayouts.push_back(*globalDescSetLayout);
		uint32_t globalDataBindingIdx = globalDescSet.addBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT);
		globalDescSet.create(app->dpool, {&particlePipeline});

		VkDescriptorBufferInfo bufferInfo { globalBuffer, 0, 16*4 };
		globalDescSet.update(VkWriteDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
				globalDescSet, 0,
				0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				nullptr, &bufferInfo, nullptr
				});
	}

	float viewportXYWH[4] = {
		0,0,
		(float)app->windowWidth,
		(float)app->windowHeight
	};

	{
		dprint("\t - create particlePipeline\n");
		loadShader(app->mainDevice, particlePipeline.vs, particlePipeline.fs, "particleCloud/particle");

		VertexInputDescription vid;
		VkVertexInputAttributeDescription attrPos       { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
		VkVertexInputAttributeDescription attrIntensity { 1, 0, VK_FORMAT_R32_SFLOAT, 3*4 };
		vid.attributes = { attrPos, attrIntensity };
		vid.bindings = { VkVertexInputBindingDescription { 0, 4*4, VK_VERTEX_INPUT_RATE_VERTEX } };

		PipelineBuilder builder;
		builder.additiveBlending = true;
		builder.depthWrite = false;
		builder.depthTest = true;
		builder.init(vid, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, particlePipeline.vs, particlePipeline.fs);

		particlePipeline.create(app->mainDevice, viewportXYWH, builder, outputPass, 0);
	}



#if 0
	// Filter Resources (just textures and pushConstants)
	{
		std::vector<vk::DescriptorPoolSize> poolSizes = {
			vk::DescriptorPoolSize { vk::DescriptorType::eCombinedImageSampler, 2 },
			vk::DescriptorPoolSize { vk::DescriptorType::eUniformBuffer, 2 },
		};
		vk::DescriptorPoolCreateInfo poolInfo {
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // allow raii to free the owned sets
				2u + 1u + app->scNumImages,
				(uint32_t)poolSizes.size(), poolSizes.data()
		};
		descPool = std::move(vk::raii::DescriptorPool(app->deviceGpu, poolInfo));

		// Bind textures. There are two sets of one binding each.
		{
			std::vector<vk::DescriptorSetLayoutBinding> bindings;
			// Texture array binding
			bindings.push_back({
					0, vk::DescriptorType::eCombinedImageSampler,
					1, vk::ShaderStageFlagBits::eFragment });

			vk::DescriptorSetLayoutCreateInfo layInfo { {}, (uint32_t)bindings.size(), bindings.data() };
			filterDescSetLayout = std::move(app->deviceGpu.createDescriptorSetLayout(layInfo));

			vk::DescriptorSetAllocateInfo allocInfo {
				*descPool, 1, &*filterDescSetLayout
			};

			for (int i=0; i<2; i++) {
				filterDescSet[i] = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo)[0]);

				// descSet is allocated, now make the arrays point correctly on the gpu side.
				std::vector<vk::DescriptorImageInfo> i_infos;

				i_infos.push_back(vk::DescriptorImageInfo{
						*filterImages[i].sampler,
						*filterImages[i].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
						});


				vk::WriteDescriptorSet writeDesc[1] = {
					{
						*filterDescSet[i],
						0, 0, (uint32_t)i_infos.size(),
						vk::DescriptorType::eCombinedImageSampler,
						i_infos.data(),
						nullptr,
						nullptr
					} };
				app->deviceGpu.updateDescriptorSets({1, writeDesc}, nullptr);
			}

			// particleDescSet is used to sample the first down texture.
			std::vector<vk::DescriptorSetLayout> layouts;
			for (int i=0; i<app->scNumImages; i++) layouts.push_back(*filterDescSetLayout);
			vk::DescriptorSetAllocateInfo allocInfo2 {
				*descPool, (uint32_t)app->scNumImages, layouts.data()
			};
			particleDescSet = std::move(app->deviceGpu.allocateDescriptorSets(allocInfo2));
			for (int i=0; i<app->scNumImages; i++) {
				std::vector<vk::DescriptorImageInfo> i_infos = {
					vk::DescriptorImageInfo{
						*particleImages[i].sampler,
						*particleImages[i].view,
						vk::ImageLayout::eShaderReadOnlyOptimal
						}};
				vk::WriteDescriptorSet writeDesc = {
						*particleDescSet[i],
						0, 0, (uint32_t)i_infos.size(),
						vk::DescriptorType::eCombinedImageSampler,
						i_infos.data(),
						nullptr,
						nullptr
					};
				app->deviceGpu.updateDescriptorSets({1, &writeDesc}, nullptr);
			}
		}
	}

	// Particle Resources (just UBO for camera)
	{
		globalBuffer.setAsUniformBuffer(4*16, true);
		globalBuffer.memPropFlags |= vk::MemoryPropertyFlagBits::eHostCoherent;
		globalBuffer.create(app->deviceGpu,*app->pdeviceGpu,app->queueFamilyGfxIdxs);
		//camAndMetaBuffer.upload(viewProj, 16*4);

		vk::DescriptorSetLayoutBinding bindings[1] = { {
			0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex } };

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


	// Particle
	{
		dprint("\t - create particlePipelineStuff\n");
		particlePipelineStuff.setup_viewport(w, h);
		std::string vsrcPath = "../frastVk/shaders/particleCloud/particle.v.glsl";
		std::string fsrcPath = "../frastVk/shaders/particleCloud/particle.f.glsl";
		// createShaderFromFiles(app->deviceGpu, particlePipelineStuff.vs, particlePipelineStuff.fs, vsrcPath, fsrcPath);
		loadShader(app->deviceGpu, particlePipelineStuff.vs, particlePipelineStuff.fs, "particleCloud/particle");

		particlePipelineStuff.setLayouts.push_back(*globalDescSetLayout);

		vk::VertexInputAttributeDescription posAttr, intensityAttr;
		posAttr.binding = 0;
		posAttr.location = 0;
		posAttr.offset = 0;
		posAttr.format = vk::Format::eR32G32B32Sfloat;
		intensityAttr.binding = 0;
		intensityAttr.location = 1;
		intensityAttr.offset = 3*4;
		intensityAttr.format = vk::Format::eR32Sfloat;
		vk::VertexInputBindingDescription mainBinding = {};
		mainBinding.binding = 0;
		mainBinding.stride = 4*4;
		mainBinding.inputRate = vk::VertexInputRate::eVertex;
		VertexInputDescription particleVertexDesc;
		particleVertexDesc.attributes = { posAttr, intensityAttr };
		particleVertexDesc.bindings = { mainBinding };


		PipelineBuilder builder;
		builder.init(
				particleVertexDesc,
				vk::PrimitiveTopology::ePointList,
				*particlePipelineStuff.vs, *particlePipelineStuff.fs);
		particlePipelineStuff.build(builder, app->deviceGpu, *particlePass, 0);
	}


	vk::raii::ShaderModule downVs{nullptr}, downFs{nullptr},
		upVs{nullptr}, upFs{nullptr},
		outputVs{nullptr}, outputFs{nullptr};
	loadShader(app->deviceGpu, downVs, downFs, "particleCloud/down");
	loadShader(app->deviceGpu, upVs, upFs, "particleCloud/down", "particleCloud/up");
	loadShader(app->deviceGpu, outputVs, outputFs, "particleCloud/down", "particleCloud/output");

	uint32_t ww = w, hh = h;

	// I think I can acheive the sub-AOI based windowing with the VkRenderPassBeginInfo renderArea field,
	// rather than having pipelines for each viewport size
	{
			dprint("\t - create downPipelineStuff\n");
			downPipelineStuff.setup_viewport(ww, hh);

			downPipelineStuff.setLayouts.push_back(*filterDescSetLayout);

			downPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					0,
					sizeof(ParticleCloudPushConstants) });

			PipelineBuilder builder;
			builder.additiveBlending = true;
			builder.depthTest = false;
			builder.depthWrite = false;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*downVs, *downFs);
			downPipelineStuff.build(builder, app->deviceGpu, *filterPass, 0);
	}
	{
			dprint("\t - create upPipelineStuff\n");
			upPipelineStuff.setup_viewport(ww, hh);

			upPipelineStuff.setLayouts.push_back(*filterDescSetLayout);

			upPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					0,
					sizeof(ParticleCloudPushConstants) });

			PipelineBuilder builder;
			builder.additiveBlending = true;
			builder.depthTest = false;
			builder.depthWrite = false;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*upVs, *upFs);
			upPipelineStuff.build(builder, app->deviceGpu, *filterPass, 0);
	}
	{
			dprint("\t - create outputPipelineStuff\n");
			outputPipelineStuff.setup_viewport(ww, hh);

			outputPipelineStuff.setLayouts.push_back(*filterDescSetLayout);

			outputPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					0,
					sizeof(ParticleCloudPushConstants) });

			PipelineBuilder builder;
			builder.additiveBlending = false;
			// builder.additiveBlending = true;
			builder.depthTest = false;
			builder.depthWrite = false;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*outputVs, *outputFs);
			outputPipelineStuff.build(builder, app->deviceGpu, *outputPass, 0);
	}
#endif
}



void ParticleCloudRenderer::uploadParticles(std::vector<float>& particles, bool normalizeMaxInplace, float divisor) {
	// Resize to twice the input, if needed
	// WARNING: This is not thread safe
	if (particles.size()/4 > capacity)
		setup_particle_buffer(2*particles.size()/4);

	float m = 1e-9;
	if (normalizeMaxInplace) {
		for (int i=3; i<particles.size(); i+=4) m = std::max(m, particles[i]);
		m = m * divisor;
		for (int i=3; i<particles.size(); i+=4) particles[i] /= m;
	}
	app->generalUploader.enqueueUpload(particleBuffer, (void*)particles.data(), particles.size()*sizeof(float), 0);
	app->generalUploader.execute();
	numParticles = particles.size() / 4;
}

void ParticleCloudRenderer::render(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer) {
	auto &fd = *rs.frameData;

	if (mode == ParticleRenderMode::eNone) return;

	// No longer valid, because we do not submit any commands in the new impl
	// AtomicTimerMeasurement atm { timer_pc };

	// Update @globalBuffer, which is kept mapped
	float mvp[16];
	rs.mvpf(mvp);
	memcpy(globalBuffer.mappedAddr, mvp, 16*4);

	// We only support ePoints right now:

	{
		outputPass.beginWithExternalFbo(cmd, *rs.frameData, outputFramebuffer);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline.layout, 0, 1, &globalDescSet.dset, 0, 0);
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &particleBuffer.buf, &offset);
		vkCmdDraw(cmd, numParticles, 1, 0, 0);

		outputPass.end(cmd, *rs.frameData);
	}






	/*
	if (mode == ParticleRenderMode::eNone) {
		vk::raii::CommandBuffer& cmd = cmdBuffers[rs.frameData->scIndx];
		cmd.reset();
		vk::CommandBufferBeginInfo beginInfo { {}, {} };
		vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
		vk::ClearValue clears_[1] = {
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.0f,.0f,.0f } } }, // color
		};
		vk::RenderPassBeginInfo rpInfo {
			*particlePass, *particleFramebuffers[rs.frameData->scIndx], aoi, {1, clears_}
		};
		cmd.begin(beginInfo);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.endRenderPass();
		cmd.end();
		return;
	}

	else if (mode == ParticleRenderMode::eFiltered) renderFiltered(rs, outputFramebuffer);
	else if (mode == ParticleRenderMode::ePoints) renderPoints(rs, outputFramebuffer);
	*/
}

void ParticleCloudRenderer::renderFiltered(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer) {
#if 0
	auto &fd = *rs.frameData;

	/*
	std::vector<uint8_t> img0(w*h*4*4,0);
	// std::vector<float> img0(w*h*1,0);
	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {
			// img0[y*w*4+x*4 +0] = (((x/32) + (y/32))%2 == 0) ? 240 : 130;
			// img0[y*w*4+x*4 +1] = (((x/32) + (y/32))%2 == 0) ? 240 : 130;
			// img0[y*w*4+x*4 +2] = (((x/32) + (y/32))%2 == 0) ? 240 : 130;
			// img0[y*w*4+x*4 +3] = (((x/32) + (y/32))%2 == 0) ? 140 : 30;
			// img0[y*w*1+x*1 +0] = (((x/32) + (y/32))%2 == 0) ? .9f : .1f;
		}
	}
	app->uploader.uploadSync(filterImages[0], img0.data(), img0.size(), 0);
	app->uploader.uploadSync(filterImages[1], img0.data(), img0.size(), 0);
	*/
	// vk::raii::CommandBuffer& cmd = cmdBuffers[rs.frameData->scIndx];

	vk::CommandBufferBeginInfo beginInfo { {}, {} };

	// TODO: Try to use just two passes with many subpasses for each lvl.
	cmd.reset();
	cmd.begin(beginInfo);

	ParticleCloudPushConstants pushc { (float)w, (float)h, 1.0f, .5f };

	// dprint("render parts\n");
	// Render particles
	if (1) {
		vk::ImageMemoryBarrier barrier;
		barrier.image = *particleImages[rs.frameData->scIndx].image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});


		vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
		vk::ClearValue clears_[1] = {
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.0f,.0f,.0f } } }, // color
		};
		vk::RenderPassBeginInfo rpInfo {
			*particlePass, *particleFramebuffers[rs.frameData->scIndx], aoi, {1, clears_}
		};

		float mvp[16];
		rs.mvpf(mvp);
		void* dbuf = (void*) globalBuffer.mem.mapMemory(0, 16*4, {});
		memcpy(dbuf, mvp, 16*4);
		globalBuffer.mem.unmapMemory();

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *particlePipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *particlePipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*particleBuffer.buffer}, {0u});
		cmd.draw(numParticles, 1, 0,0);
		cmd.endRenderPass();
	}


	// float dstep = .1f;
	pushc.d = 1.0f;
	pushc.d = .3f;
	float d_gamma = .8f;

	// Do down filtering
	// dprint("down\n");
	int lvl_i = 0;
	for (; lvl_i<n_lvl; lvl_i++)
	{
		vk::Image inImg, outImg;
		vk::DescriptorSet inSet;
		vk::Framebuffer outFbo;

		if (lvl_i == 0) {
			inImg = *particleImages[fd.scIndx].image;
			inSet = *particleDescSet[fd.scIndx];
			outFbo = *filterFramebuffers[(1+lvl_i)%2];
			outImg = *filterImages[(lvl_i)%2].image;
		} else {
			inImg = *filterImages[lvl_i%2].image;
			inSet = *filterDescSet[lvl_i%2];
			outFbo = *filterFramebuffers[(1+lvl_i)%2];
			outImg = *filterImages[(1+lvl_i)%2].image;
		}

		// if (lvl_i == 0) {
		// if (true) {
		if (true) {
			vk::ImageMemoryBarrier barrier;
			barrier.image = inImg;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});

			vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
			vk::RenderPassBeginInfo rpInfo {
				*outputPass, outputFramebuffer, aoi, {}
			};
			// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipeline);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipelineLayout, 0, {1,&inSet}, nullptr);
			ParticleCloudPushConstants pushc2;
			pushc2 = pushc;
			pushc2.s = 1.0f;
			pushc2.w = pushc.s;
			if (lvl_i == 0) pushc2.d = .9f;
			cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc2});
			cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
			cmd.draw(6, 1, 0, 0);
			// for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
			cmd.endRenderPass();
		}

		pushc.w /= 2.0f;
		pushc.h /= 2.0f;
		pushc.s /= 2.0f;
		// pushc.d -= dstep;
		pushc.d *= d_gamma;


		vk::Rect2D aoi { { 0, 0 }, { (uint32_t)pushc.w, (uint32_t)pushc.h } };
		vk::ClearValue clears_[1] = { vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.0f,.0f,.0f } } } };
		vk::RenderPassBeginInfo rpInfo { *filterPass, outFbo, aoi, {1, clears_} };
		// vk::RenderPassBeginInfo rpInfo { *filterPass, outFbo, aoi, {} };

		vk::ImageMemoryBarrier barrier;
		barrier.image = inImg;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		// dprint(" - [pc] down with sz {} {} | i {}\n", pushc.w, pushc.h, lvl_i);


		{
			vk::ImageMemoryBarrier barrier;
			barrier.image = outImg;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		}

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *downPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *downPipelineStuff.pipelineLayout, 0, {1,&inSet}, nullptr);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.pushConstants(*downPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc});
		cmd.draw(6, 1, 0,0);
		cmd.endRenderPass();




		// barrier.oldLayout = vk::ImageLayout::eUndefined;
		// barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, {}, {}, {}, {1, &barrier});
	}

	// pushc.d -= dstep * .5f;
	pushc.d *= 1.5f;


	// dprint("up\n");
	// Do up filtering
	for (; lvl_i>0; lvl_i--)
	{
		// continue;
		vk::Image inImg, outImg;
		vk::DescriptorSet inSet;
		vk::Framebuffer outFbo;

		// if (lvl_i == 0) {
			// inImg = *particleImages[fd.scIndx].image;
			// inSet = *particleDescSet[fd.scIndx];
			// outFbo = *filterFramebuffers[(1+lvl_i)%2];
		//} else {
			inImg = *filterImages[lvl_i%2].image;
			inSet = *filterDescSet[lvl_i%2];
			outFbo = *filterFramebuffers[(1+lvl_i)%2];
			outImg = *filterImages[(1+lvl_i)%2].image;
		//}

		if (true) {
		// if (false) {
			vk::ImageMemoryBarrier barrier;
			barrier.image = inImg;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});

			vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
			vk::RenderPassBeginInfo rpInfo {
				*outputPass, outputFramebuffer, aoi, {}
			};
			// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipeline);
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipelineLayout, 0, {1,&inSet}, nullptr);
			ParticleCloudPushConstants pushc2;
			pushc2 = pushc;
			pushc2.w = pushc.s;
			pushc2.s = 1.0f;
			cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc2});
			cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
			cmd.draw(6, 1, 0, 0);
			// for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
			cmd.endRenderPass();
		}

		pushc.w *= 2.0f;
		pushc.h *= 2.0f;
		pushc.s *= 2.0f;
		pushc.d *= 1.f / d_gamma;

		vk::Rect2D aoi { { 0, 0 }, { (uint32_t)pushc.w, (uint32_t)pushc.h } };
		vk::ClearValue clears_[1] = { vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.0f,.0f,.0f } } } };
		vk::RenderPassBeginInfo rpInfo { *filterPass, outFbo, aoi, {1, clears_} };
		// vk::RenderPassBeginInfo rpInfo { *filterPass, outFbo, aoi, {} };

		if (0) {
			vk::ImageMemoryBarrier barrier;
			barrier.image = inImg;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		}


		{
			vk::ImageMemoryBarrier barrier;
			barrier.image = outImg;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		}

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *upPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *upPipelineStuff.pipelineLayout, 0, {1,&inSet}, nullptr);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.pushConstants(*upPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc});
		cmd.draw(6, 1, 0,0);
		cmd.endRenderPass();

		if (1) {
			vk::ImageMemoryBarrier barrier;
			barrier.image = inImg;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		}
	}

	lvl_i--;

	// pushc.d -= dstep;

	// dprint("final\n");
	// Draw new texture ontop of old fbo
	if (1) {
		vk::ImageMemoryBarrier barrier;
		barrier.image = *filterImages[(lvl_i+1)%2].image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eUndefined;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});

		vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
		vk::RenderPassBeginInfo rpInfo {
			*outputPass, outputFramebuffer, aoi, {}
		};
		// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipelineLayout, 0, {1,&*filterDescSet[(lvl_i+1)%2]}, nullptr);
		pushc.w = 1.0f;
		pushc.h = h;
		pushc.s = 1.0f;
		pushc.d = .6f;
		cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc});
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.draw(6, 1, 0, 0);
		// for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
		cmd.endRenderPass();
	}


	// dprint("done\n");
	cmd.end();
	return;
#endif
}

void ParticleCloudRenderer::renderPoints(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer) {
#if 0
	auto &fd = *rs.frameData;
	// vk::raii::CommandBuffer& cmd = cmdBuffers[rs.frameData->scIndx];

	vk::CommandBufferBeginInfo beginInfo { {}, {} };


	// TODO: Try to use just two passes with many subpasses for each lvl.
	cmd.reset();
	cmd.begin(beginInfo);

	ParticleCloudPushConstants pushc { (float)w, (float)h, 1.0f, 1.0f };

	{
	vk::ImageMemoryBarrier barrier;
	barrier.image = *particleImages[rs.frameData->scIndx].image;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
	}

	// Render particles
	{
		vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
		vk::ClearValue clears_[1] = {
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.0f,.0f,.0f } } }, // color
		};
		vk::RenderPassBeginInfo rpInfo {
			*particlePass, *particleFramebuffers[rs.frameData->scIndx], aoi, {1, clears_}
		};

		float mvp[16];
		rs.mvpf(mvp);
		void* dbuf = (void*) globalBuffer.mem.mapMemory(0, 16*4, {});
		memcpy(dbuf, mvp, 16*4);
		globalBuffer.mem.unmapMemory();

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *particlePipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *particlePipelineStuff.pipelineLayout, 0, {1,&*globalDescSet}, nullptr);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.bindVertexBuffers(0, vk::ArrayProxy<const vk::Buffer>{1, &*particleBuffer.buffer}, {0u});
		// fmt::print(" - Drawing {} parts\n", numParticles);
		cmd.draw(numParticles, 1, 0,0);
		cmd.endRenderPass();
	}



	{
		vk::ImageMemoryBarrier barrier;
		barrier.image = *particleImages[rs.frameData->scIndx].image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});


		vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
		vk::RenderPassBeginInfo rpInfo {
			*outputPass, outputFramebuffer, aoi, {}
			// *outputPass, *particleFramebuffers[rs.frameData->scIndx], aoi, {}
		};
		// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipelineLayout, 0, {1,&*particleDescSet[rs.frameData->scIndx]}, nullptr);

			ParticleCloudPushConstants pushc2;
			pushc2 = pushc;
			pushc2.s = 1.0f;
			pushc2.w = pushc.s;
			pushc2.d = 1.0f;
			cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc2});

			/*
		barrier.image = app->sc.getImage(rs.frameData->scIndx);
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eUndefined;
		barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		*/

		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);



		cmd.draw(6, 1, 0, 0);
		// for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
		cmd.endRenderPass();

	}

		vk::ImageMemoryBarrier barrier;
		barrier.image = app->sc.getImage(rs.frameData->scIndx);
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
		// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});

	cmd.end();
	return *cmd;
#endif
}
