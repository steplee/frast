#include "particleCloud.h"

#include "core/render_state.h"

#include <fmt/core.h>
#include <fmt/color.h>

#define USE_TIMER
#include <chrono>
#include <frast/utils/timer.hpp>
static AtomicTimer timer_pc("pc::render");

#define print(...) fmt::print(fmt::fg(fmt::color::medium_slate_blue), __VA_ARGS__);
//#define print(...) {}


ParticleCloudRenderer::ParticleCloudRenderer(BaseVkApp* app) : app(app) {
	setup();
}

void ParticleCloudRenderer::setup() {
	w = app->windowWidth;
	h = app->windowHeight;

	cmdBuffers = std::move(app->deviceGpu.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
				*app->commandPool,
				vk::CommandBufferLevel::ePrimary,
				(uint32_t)(app->scNumImages) }));

	setup_buffers();
	setup_fbos();
	setup_pipelines();
}

void ParticleCloudRenderer::setup_buffers() {
	particleBuffer.setAsVertexBuffer(1024*4, false);
	particleBuffer.create(app->deviceGpu, *app->pdeviceGpu, app->queueFamilyGfxIdxs);
}

void ParticleCloudRenderer::setup_fbos() {
	print(" - setting up fbos\n");
	std::vector<uint8_t> emptyImage(h*w*4, 0);

	// 0) Setup output pass.
	{
		vk::AttachmentDescription colorAttachment {
			{}, app->scSurfaceFormat.format,
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eUndefined,
				//vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::ePresentSrcKHR };
		vk::AttachmentReference colorAttachmentRef { 0, vk::ImageLayout::eColorAttachmentOptimal };

		vk::AttachmentDescription depthAttachment {
			{}, vk::Format { app->simpleRenderPass.depthImages[0].format },
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, // NOTE: Don't write to depth, only read!
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eUndefined,
				//vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eDepthStencilAttachmentOptimal };
		vk::AttachmentReference depthAttachmentRef { 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };

		std::vector<vk::SubpassDescription> subpasses {
			{ {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef },
			{ {}, vk::PipelineBindPoint::eGraphics, { }, { 1, &colorAttachmentRef }, { }, &depthAttachmentRef },
		};
		vk::SubpassDependency depthDependency { VK_SUBPASS_EXTERNAL, 0,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
				vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
				{}, vk::AccessFlagBits::eDepthStencilAttachmentWrite, {} };
		std::vector<vk::SubpassDependency> dependencies = {
		// Depth
		{ VK_SUBPASS_EXTERNAL, 0,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
			{}, vk::AccessFlagBits::eDepthStencilAttachmentWrite, {} },
		// 0-1
		{
			0, 1, vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics,
			vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::DependencyFlagBits::eDeviceGroup }
	};

		vk::AttachmentDescription atts[2] = { colorAttachment, depthAttachment };
		vk::RenderPassCreateInfo rpInfo {
			{},
				{ 2, atts },
				subpasses,
				dependencies
		};
		print("\t - create output pass\n");
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
				//vk::AttachmentLoadOp::eStore, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eUndefined,
				//vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::ePresentSrcKHR };

		vk::AttachmentReference colorAttachmentRef {
			0,
				vk::ImageLayout::eColorAttachmentOptimal
		};

		// Depth
		vk::AttachmentDescription depthAttachment {
			{},
				vk::Format { app->simpleRenderPass.depthImages[0].format },
				vk::SampleCountFlagBits::e1,
				vk::AttachmentLoadOp::eDontCare,
				// vk::AttachmentStoreOp::eStore,
				vk::AttachmentStoreOp::eDontCare, // NOTE: Don't write to depth, only read!
				vk::AttachmentLoadOp::eDontCare,
				vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eUndefined,
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


		// Depth dependency
		/*SubpassDependency( uint32_t                                 srcSubpass_      = {},
		  uint32_t                                 dstSubpass_      = {},
		  VULKAN_HPP_NAMESPACE::PipelineStageFlags srcStageMask_    = {},
		  VULKAN_HPP_NAMESPACE::PipelineStageFlags dstStageMask_    = {},
		  VULKAN_HPP_NAMESPACE::AccessFlags        srcAccessMask_   = {},
		  VULKAN_HPP_NAMESPACE::AccessFlags        dstAccessMask_   = {},
		  VULKAN_HPP_NAMESPACE::DependencyFlags    dependencyFlags_ = {} ) VULKAN_HPP_NOEXCEPT*/
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
		print("\t - create particle pass\n");
		particlePass = std::move(app->deviceGpu.createRenderPass(rpInfo));


		/*VULKAN_HPP_CONSTEXPR FramebufferCreateInfo( VULKAN_HPP_NAMESPACE::FramebufferCreateFlags flags_           = {},
		  VULKAN_HPP_NAMESPACE::RenderPass             renderPass_      = {},
		  uint32_t                                     attachmentCount_ = {},
		  const VULKAN_HPP_NAMESPACE::ImageView *      pAttachments_    = {},
		  uint32_t                                     width_           = {},
		  uint32_t                                     height_          = {},
		  uint32_t layers_ = {} ) VULKAN_HPP_NOEXCEPT*/
		print("\t - create particle images\n");
		for (int i=0; i<app->scNumImages; i++) {
			particleImages.push_back(ResidentImage{});
			particleImages.back().unnormalizedCoordinates = true;
			particleImages.back().createAsTexture(app->uploader, h, w,
					// app->scSurfaceFormat.format,
					// vk::Format::eR32Sfloat,
					vk::Format::eR32G32B32A32Sfloat,
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
				// vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore,
				vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
				vk::ImageLayout::eUndefined,
				// vk::ImageLayout::eShaderReadOnlyOptimal };
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

			print("\t - create filterImages[{}]\n", jj);
			/*
			for (int i=0; i<app->scNumImages; i++) {
				filterImages[jj].push_back(ResidentImage{});
				filterImages[jj].back().createAsTexture(app->uploader, h, w,
					app->scSurfaceFormat.format, emptyImage.data(), vk::ImageUsageFlagBits::eColorAttachment);
			}
			*/
			filterImages[jj].unnormalizedCoordinates = true;
			filterImages[jj].createAsTexture(app->uploader, h, w,
					// app->scSurfaceFormat.format, emptyImage.data(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
					vk::Format::eR32Sfloat, emptyImage.data(), vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
					vk::SamplerAddressMode::eClampToBorder);

			print("\t - create filterFramebuffers[{}]\n", jj);
			/*
			for (int i=0; i<app->scNumImages; i++) {
				vk::ImageView views[1] = { *filterImages[jj][i].view };

				vk::FramebufferCreateInfo fbInfo {
					{},
						*filterPass,
						{ 1, views},
						w, h,
						1
				};
				filterFramebuffers[jj].push_back(std::move(app->deviceGpu.createFramebuffer(fbInfo)));
			}
			*/
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

}

void ParticleCloudRenderer::setup_pipelines() {
	print(" - [ParticleCloudRenderer] setup pipelines\n");

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
		print("\t - create particlePipelineStuff\n");
		particlePipelineStuff.setup_viewport(w, h);
		std::string vsrcPath = "../src/shaders/particleCloud/particle.v.glsl";
		std::string fsrcPath = "../src/shaders/particleCloud/particle.f.glsl";
		createShaderFromFiles(app->deviceGpu, particlePipelineStuff.vs, particlePipelineStuff.fs, vsrcPath, fsrcPath);

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
	std::string vsrcPath1 = "../src/shaders/particleCloud/down.v.glsl";
	std::string fsrcPath1 = "../src/shaders/particleCloud/down.f.glsl";
	createShaderFromFiles(app->deviceGpu, downVs, downFs, vsrcPath1, fsrcPath1);
	std::string vsrcPath2 = "../src/shaders/particleCloud/down.v.glsl";
	std::string fsrcPath2 = "../src/shaders/particleCloud/up.f.glsl";
	createShaderFromFiles(app->deviceGpu, upVs, upFs, vsrcPath2, fsrcPath2);
	std::string vsrcPath3 = "../src/shaders/particleCloud/down.v.glsl";
	std::string fsrcPath3 = "../src/shaders/particleCloud/output.f.glsl";
	createShaderFromFiles(app->deviceGpu, outputVs, outputFs, vsrcPath3, fsrcPath3);

	uint32_t ww = w, hh = h;

	/*
	// Down
	for (int l=0; l<n_lvl; l++) {
			downPipelineStuffs[l].setup_viewport(ww, hh);

			PipelineBuilder builder;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*downVs, *downFs);
			downPipelineStuffs[l].build(builder, app->deviceGpu, *filterPass);

			ww >>= 1; hh >>= 1;
			assert(ww > 1);
			assert(hh > 1);
	}

	// Up
	for (int l=0; l<n_lvl; l++) {
			upPipelineStuffs[l].setup_viewport(ww, hh);

			PipelineBuilder builder;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*upVs, *upFs);
			upPipelineStuffs[l].build(builder, app->deviceGpu, *filterPass);

			ww *= 2; hh *= 2;
	}
	*/

	// I think I can acheive the sub-AOI based windowing with the VkRenderPassBeginInfo renderArea field,
	// rather than having pipelines for each viewport size
	{
			print("\t - create downPipelineStuff\n");
			downPipelineStuff.setup_viewport(ww, hh);

			downPipelineStuff.setLayouts.push_back(*filterDescSetLayout);

			downPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					0,
					sizeof(ParticleCloudPushConstants) });

			PipelineBuilder builder;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*downVs, *downFs);
			downPipelineStuff.build(builder, app->deviceGpu, *filterPass, 0);
	}
	{
			print("\t - create upPipelineStuff\n");
			upPipelineStuff.setup_viewport(ww, hh);

			upPipelineStuff.setLayouts.push_back(*filterDescSetLayout);

			upPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
					vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
					0,
					sizeof(ParticleCloudPushConstants) });

			PipelineBuilder builder;
			builder.init(
					{},
					vk::PrimitiveTopology::eTriangleList,
					*upVs, *upFs);
			upPipelineStuff.build(builder, app->deviceGpu, *filterPass, 0);
	}
	{
			print("\t - create outputPipelineStuff\n");
			outputPipelineStuff.setup_viewport(ww, hh);

			outputPipelineStuff.setLayouts.push_back(*filterDescSetLayout);

			outputPipelineStuff.pushConstants.push_back(vk::PushConstantRange{
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
					*outputVs, *outputFs);
			outputPipelineStuff.build(builder, app->deviceGpu, *outputPass, 0);
	}
}



void ParticleCloudRenderer::uploadParticles(const std::vector<float>& particles) {
	app->uploader.uploadSync(particleBuffer, (void*)particles.data(), particles.size()*sizeof(float), 0);
	numParticles = particles.size() / 4;
}

vk::CommandBuffer ParticleCloudRenderer::render(RenderState& rs, vk::Framebuffer outputFramebuffer) {
	auto &fd = *rs.frameData;

	AtomicTimerMeasurement atm { timer_pc };

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
		return *cmd;
	}

	else if (mode == ParticleRenderMode::eFiltered) return renderFiltered(rs, outputFramebuffer);
	else return renderPoints(rs, outputFramebuffer);
}

vk::CommandBuffer ParticleCloudRenderer::renderFiltered(RenderState& rs, vk::Framebuffer outputFramebuffer) {
	auto &fd = *rs.frameData;

	/*
	// std::vector<uint8_t> img0(w*h*4,0);
	std::vector<float> img0(w*h*1,0);
	for (int y=0; y<h; y++) {
		for (int x=0; x<w; x++) {
			// img0[y*w*4+x*4 +0] = (((x/32) + (y/32))%2 == 0) ? 240 : 130;
			// img0[y*w*4+x*4 +1] = (((x/32) + (y/32))%2 == 0) ? 240 : 130;
			// img0[y*w*4+x*4 +2] = (((x/32) + (y/32))%2 == 0) ? 240 : 130;
			// img0[y*w*4+x*4 +3] = (((x/32) + (y/32))%2 == 0) ? 140 : 30;
			// img0[y*w*1+x*1 +0] = (((x/32) + (y/32))%2 == 0) ? .9f : .1f;
		}
	}
	app->uploader.uploadSync(filterImages[0], img0.data(), 4*img0.size(), 0);
	app->uploader.uploadSync(filterImages[1], img0.data(), 4*img0.size(), 0);
	*/

	vk::raii::CommandBuffer& cmd = cmdBuffers[rs.frameData->scIndx];

	vk::CommandBufferBeginInfo beginInfo { {}, {} };

	// TODO: Try to use just two passes with many subpasses for each lvl.
	cmd.reset();
	cmd.begin(beginInfo);

	ParticleCloudPushConstants pushc { w, h, 1.0f, .5f };

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
		cmd.draw(numParticles, 1, 0,0);
		cmd.endRenderPass();
	}

	// float dstep = .1f;
	pushc.d = 0.0f;

	// Do down filtering
	int lvl_i = 0;
	for (; lvl_i<n_lvl; lvl_i++)
	{
		vk::Image inImg;
		vk::DescriptorSet inSet;
		vk::Framebuffer outFbo;

		if (lvl_i == 0) {
			inImg = *particleImages[fd.scIndx].image;
			inSet = *particleDescSet[fd.scIndx];
			outFbo = *filterFramebuffers[(1+lvl_i)%2];
		} else {
			inImg = *filterImages[lvl_i%2].image;
			inSet = *filterDescSet[lvl_i%2];
			outFbo = *filterFramebuffers[(1+lvl_i)%2];
		}

		if (lvl_i == 0) {
			vk::ImageMemoryBarrier barrier;
			barrier.image = inImg;
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
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipelineLayout, 0, {1,&inSet}, nullptr);
			ParticleCloudPushConstants pushc2;
			pushc2 = pushc;
			pushc2.s = 1.0f;
			pushc2.d = .4f;
			cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc2});
			cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
			cmd.draw(6, 1, 0, 0);
			for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
			cmd.endRenderPass();
		}

		pushc.w >>= 1;
		pushc.h >>= 1;
		pushc.s /= 2.0f;
		// pushc.d -= dstep;
		pushc.d = .2f;


		vk::Rect2D aoi { { 0, 0 }, { pushc.w, pushc.h } };
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
		barrier.oldLayout = vk::ImageLayout::eUndefined;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		// print(" - [pc] down with sz {} {} | i {}\n", pushc.w, pushc.h, lvl_i);

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


	// Do up filtering
	for (; lvl_i>0; lvl_i--)
	{
		vk::Image inImg;
		vk::DescriptorSet inSet;
		vk::Framebuffer outFbo;

		pushc.w <<= 1;
		pushc.h <<= 1;
		pushc.s *= 2.0f;
		// pushc.d -= dstep;

		// if (lvl_i == 0) {
			// inImg = *particleImages[fd.scIndx].image;
			// inSet = *particleDescSet[fd.scIndx];
			// outFbo = *filterFramebuffers[(1+lvl_i)%2];
		//} else {
			inImg = *filterImages[lvl_i%2].image;
			inSet = *filterDescSet[lvl_i%2];
			outFbo = *filterFramebuffers[(1+lvl_i)%2];
		//}

		vk::Rect2D aoi { { 0, 0 }, { pushc.w, pushc.h } };
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
		barrier.oldLayout = vk::ImageLayout::eUndefined;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		// print(" - [pc] up   with sz {} {} | i {}\n", pushc.w, pushc.h, lvl_i);

		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *upPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *upPipelineStuff.pipelineLayout, 0, {1,&inSet}, nullptr);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.pushConstants(*upPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc});
		cmd.draw(6, 1, 0,0);
		cmd.endRenderPass();
	}

	lvl_i--;

	// pushc.d -= dstep;

	// Draw new texture ontop of old fbo
	{
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
		pushc.w = w;
		pushc.h = h;
		pushc.s = 1.0f;
		pushc.d = .6f;
		cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc});
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.draw(6, 1, 0, 0);
		for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
		cmd.endRenderPass();
	}

	cmd.end();
	return *cmd;
}

vk::CommandBuffer ParticleCloudRenderer::renderPoints(RenderState& rs, vk::Framebuffer outputFramebuffer) {
	auto &fd = *rs.frameData;
	vk::raii::CommandBuffer& cmd = cmdBuffers[rs.frameData->scIndx];

	vk::CommandBufferBeginInfo beginInfo { {}, {} };

	// TODO: Try to use just two passes with many subpasses for each lvl.
	cmd.reset();
	cmd.begin(beginInfo);

	ParticleCloudPushConstants pushc { w, h, 1.0f };

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
		barrier.oldLayout = vk::ImageLayout::eUndefined;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});

		vk::Rect2D aoi { { 0, 0 }, { app->windowWidth, app->windowHeight } };
		vk::RenderPassBeginInfo rpInfo {
			*outputPass, outputFramebuffer, aoi, {}
		};
		// cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics, vk::DependencyFlagBits::eDeviceGroup, {}, {}, {1, &barrier});
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *outputPipelineStuff.pipelineLayout, 0, {1,&*particleDescSet[rs.frameData->scIndx]}, nullptr);
		pushc.w = w;
		pushc.h = h;
		pushc.s = 1.0f;
		cmd.pushConstants(*outputPipelineStuff.pipelineLayout, vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const ParticleCloudPushConstants>{1, &pushc});
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		cmd.draw(6, 1, 0, 0);
		for (int i=0; i<app->mainSubpass(); i++) cmd.nextSubpass(vk::SubpassContents::eInline);
		cmd.endRenderPass();
	}

	cmd.end();
	return *cmd;
}
