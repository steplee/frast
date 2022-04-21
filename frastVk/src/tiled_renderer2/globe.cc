
#include "core/app.h"

#include "clipmap1/clipmap1.h"

#include <sys/types.h>
#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>


// #include "tiled_renderer/tiled_renderer.h"
// using namespace tr1;
#include "tiled_renderer2/tiled_renderer.h"
using namespace tr2;

struct GlobeApp : public VkApp {

		std::shared_ptr<ClipMapRenderer1> clipmap;
		std::shared_ptr<TiledRenderer> tiledRenderer;

	inline virtual void init() override {
		VkApp::init();
		initFinalImage();
		//set position of camera offset by loaded mld ctr

		CameraSpec spec { (float)windowWidth, (float)windowHeight, 45 * 3.141 / 180. };


		if (0) {
			camera = std::make_shared<FlatEarthMovingCamera>(spec);
			alignas(16) double pos0[] { 
				(double)(-8590834.045999 / 20037508.342789248),
				(float)(4757669.951554 / 20037508.342789248),
				(double)(2.0 * 1./(1<<(7-1))) };
			alignas(16) double R0[] {
				1,0,0,
				0,-1,0,
				0,0,-1 };
			camera->setPosition(pos0);
			camera->setRotMatrix(R0);
		} else {
			camera = std::make_shared<SphericalEarthMovingCamera>(spec);
			// Eigen::Vector3d pos0 { 0,-2.0,0};
			Eigen::Vector3d pos0 { .2,-1.0,.84};
			Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
			R0.row(2) = -pos0.normalized();
			R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
			R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
			R0.transposeInPlace();
			std::cout << " - R0:\n" << R0 << "\n";
			camera->setPosition(pos0.data());
			camera->setRotMatrix(R0.data());
		}

		ioUsers.push_back(camera);
		renderState.camera = camera;

		//clipmap = std::make_shared<ClipMapRenderer1>(this);
		//clipmap->init();

		// TiledRendererCfg cfg ("/data/naip/ok/ok16.ft", "/data/elevation/gmted/gmted.ft");
		TiledRendererCfg cfg ("/data/naip/mocoNaip/out.ft", "/data/elevation/gmted/gmted.ft");
		tiledRenderer = std::make_shared<TiledRenderer>(cfg, this);
		tiledRenderer->init();
	}

	inline virtual void doRender(RenderState& rs) override {

		std::vector<vk::CommandBuffer> cmds = {
			tiledRenderer->stepAndRender(renderState)
		};

		auto& fd = *rs.frameData;
		vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;

		if (headless) {
			if (fd.n <= scNumImages) {
				vk::SubmitInfo submitInfo {
					0, nullptr, // wait sema
						&waitMask,
						(uint32_t)cmds.size(), cmds.data(),
						1, &*fd.renderCompleteSema // signal sema
				};
				queueGfx.submit(submitInfo, *fd.frameDoneFence);
			} else {
				vk::SubmitInfo submitInfo {
					// 0, nullptr, // wait sema
					1, &(*fd.scAcquireSema), // wait sema
						&waitMask,
						//1, &(*commandBuffers[fd.scIndx]),
						(uint32_t)cmds.size(), cmds.data(),
						1, &*fd.renderCompleteSema // signal sema
				};
				queueGfx.submit(submitInfo, *fd.frameDoneFence);
			}
		} else {
			vk::SubmitInfo submitInfo {
				1, &(*fd.scAcquireSema), // wait sema
				&waitMask,
				//1, &(*commandBuffers[fd.scIndx]),
				(uint32_t)cmds.size(), cmds.data(),
				1, &*fd.renderCompleteSema // signal sema
			};
			queueGfx.submit(submitInfo, *fd.frameDoneFence);
		}
	}

	inline ~GlobeApp() {
		for (auto& fd : frameDatas)
			deviceGpu.waitForFences({*fd.frameDoneFence}, true, 999999999);
	}

	ResidentImage finalImage;
	void initFinalImage() {
		if (headless) finalImage.createAsCpuVisible(uploader, windowHeight, windowWidth, vk::Format::eR8G8B8A8Uint, nullptr);
	}
	inline virtual void handleCompletedHeadlessRender(RenderState& rs) override {
		auto &fd = *rs.frameData;
		auto& copyCmd = sc.headlessCopyCmds[fd.scIndx];

		/*
    VULKAN_HPP_CONSTEXPR ImageSubresourceLayers( VULKAN_HPP_NAMESPACE::ImageAspectFlags aspectMask_     = {},
                                                 uint32_t                               mipLevel_       = {},
                                                 uint32_t                               baseArrayLayer_ = {},
                                                 uint32_t layerCount_ = {} ) VULKAN_HPP_NOEXCEPT
    VULKAN_HPP_CONSTEXPR ImageCopy( VULKAN_HPP_NAMESPACE::ImageSubresourceLayers srcSubresource_ = {},
                                    VULKAN_HPP_NAMESPACE::Offset3D               srcOffset_      = {},
                                    VULKAN_HPP_NAMESPACE::ImageSubresourceLayers dstSubresource_ = {},
                                    VULKAN_HPP_NAMESPACE::Offset3D               dstOffset_      = {},
                                    VULKAN_HPP_NAMESPACE::Extent3D               extent_ = {} ) VULKAN_HPP_NOEXCEPT
	*/

	vk::ImageCopy region {
		vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
		vk::Offset3D{},
		vk::ImageSubresourceLayers { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
		vk::Offset3D{},
		vk::Extent3D{windowWidth,windowHeight,1}
	};

		copyCmd.begin({});

		/*
      ImageMemoryBarrier( VULKAN_HPP_NAMESPACE::AccessFlags srcAccessMask_ = {},
                          VULKAN_HPP_NAMESPACE::AccessFlags dstAccessMask_ = {},
                          VULKAN_HPP_NAMESPACE::ImageLayout oldLayout_ = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined,
                          VULKAN_HPP_NAMESPACE::ImageLayout newLayout_ = VULKAN_HPP_NAMESPACE::ImageLayout::eUndefined,
                          uint32_t                          srcQueueFamilyIndex_        = {},
                          uint32_t                          dstQueueFamilyIndex_        = {},
                          VULKAN_HPP_NAMESPACE::Image       image_                      = {},
                          VULKAN_HPP_NAMESPACE::ImageSubresourceRange subresourceRange_ = {} ) VULKAN_HPP_NOEXCEPT

    VULKAN_HPP_CONSTEXPR ImageSubresourceRange( VULKAN_HPP_NAMESPACE::ImageAspectFlags aspectMask_     = {},
                                                uint32_t                               baseMipLevel_   = {},
                                                uint32_t                               levelCount_     = {},
                                                uint32_t                               baseArrayLayer_ = {},
                                                uint32_t layerCount_ = {} ) VULKAN_HPP_NOEXCEPT
    VULKAN_HPP_INLINE void CommandBuffer::pipelineBarrier(
      VULKAN_HPP_NAMESPACE::PipelineStageFlags                            srcStageMask,
      VULKAN_HPP_NAMESPACE::PipelineStageFlags                            dstStageMask,
      VULKAN_HPP_NAMESPACE::DependencyFlags                               dependencyFlags,
      ArrayProxy<const VULKAN_HPP_NAMESPACE::MemoryBarrier> const &       memoryBarriers,
      ArrayProxy<const VULKAN_HPP_NAMESPACE::BufferMemoryBarrier> const & bufferMemoryBarriers,
      ArrayProxy<const VULKAN_HPP_NAMESPACE::ImageMemoryBarrier> const & imageMemoryBarriers ) const VULKAN_HPP_NOEXCEPT*/
		std::vector<vk::ImageMemoryBarrier> imgBarriers = {
			vk::ImageMemoryBarrier {
				{},{},
				{}, vk::ImageLayout::eTransferSrcOptimal,
				{}, {},
				*sc.headlessImages[fd.scIndx],
				vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
			},
			vk::ImageMemoryBarrier {
				{},{},
				{}, vk::ImageLayout::eTransferDstOptimal,
				{}, {},
				*finalImage.image,
				vk::ImageSubresourceRange { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
			}
		};
		copyCmd.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllGraphics,
				vk::PipelineStageFlagBits::eAllGraphics,
				vk::DependencyFlagBits::eDeviceGroup,
				{}, {}, imgBarriers);



		copyCmd.copyImage(*sc.headlessImages[fd.scIndx], vk::ImageLayout::eTransferSrcOptimal, *finalImage.image, vk::ImageLayout::eTransferDstOptimal, {1,&region});
		copyCmd.end();

		// if (fd.n == 0) {
		if (true) {
			vk::PipelineStageFlags waitMasks[1] = {vk::PipelineStageFlagBits::eAllGraphics};
			const vk::Semaphore semas[1] = {(*fd.renderCompleteSema)};
			vk::SubmitInfo submitInfo {
				// {1u, &(*fd.renderCompleteSema)}, // wait sema
				{1u, semas}, // wait sema
					{1u, waitMasks},
					{1u, &*copyCmd},
					// {}
					{1u, &*fd.scAcquireSema} // signal sema
			};
			queueGfx.submit(submitInfo, {*sc.headlessCopyDoneFences[fd.scIndx]});

		} else {

			// vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;
			const vk::Semaphore semas[2] = {(*fd.renderCompleteSema), (*sc.headlessCopyDoneSemas[0])};
			vk::PipelineStageFlags waitMasks[2] = {vk::PipelineStageFlagBits::eAllGraphics,vk::PipelineStageFlagBits::eAllGraphics};
			vk::SubmitInfo submitInfo {
				// {1u, &(*fd.renderCompleteSema)}, // wait sema
				{2u, semas}, // wait sema
					{2u, waitMasks},
					{1u, &*copyCmd},
					{1u, &*fd.scAcquireSema} // signal sema
			};
			queueGfx.submit(submitInfo, {*sc.headlessCopyDoneFences[fd.scIndx]});
		}


		deviceGpu.waitForFences({*sc.headlessCopyDoneFences[fd.scIndx]}, true, 999999999999);
		deviceGpu.resetFences({*sc.headlessCopyDoneFences[fd.scIndx]});
		if (0) {
			uint8_t* dbuf = (uint8_t*) finalImage.mem.mapMemory(0, windowHeight*windowWidth*4, {});
			uint8_t* buf = (uint8_t*) malloc(windowWidth*windowHeight*3);
			for (int y=0; y<windowHeight; y++)
				for (int x=0; x<windowWidth; x++)
					for (int c=0; c<3; c++) {
						buf[y*windowWidth*3+x*3+c] = dbuf[y*windowWidth*4+x*4+c];
					}
			finalImage.mem.unmapMemory();

			auto fd_ = open("tst.bmp", O_RDWR | O_TRUNC | O_CREAT, S_IRWXU);
			write(fd_, buf, windowWidth*windowHeight*3);
			close(fd_);
			free(buf);
		}
		// memcpy(dbuf, renderState.mvp(), 16*4);

	}
};


int main() {

	GlobeApp app;
	app.windowWidth = 1000;
	app.windowHeight = 800;
	app.headless = true;
	app.headless = false;

	app.init();
	//ClipMapRenderer1 cm(&app);
	//cm.init();

	while (not app.isDone()) {
		if (not app.headless)
			bool proc = app.pollWindowEvents();
		//if (proc) app.render();
		app.render();
		//usleep(33'000);
	}

	return 0;
}
