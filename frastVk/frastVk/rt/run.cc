
#include "core/app.h"

#include "rt/rt.h"

using namespace rt;

struct RtApp : public VkApp {

		std::shared_ptr<RtRenderer> rtr;

	inline virtual void initVk() override {
		VkApp::initVk();
		//set position of camera offset by loaded mld ctr

		CameraSpec spec { (float)windowWidth, (float)windowHeight, 45 * 3.141 / 180. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);
		/*
		alignas(16) double pos0[] { 0,-2.0,0 };
		alignas(16) double R0[] {
			1,0,0,
			0,0,1,
			0,-1,0 };
		camera->setPosition(pos0);
		camera->setRotMatrix(R0);
		*/

		Eigen::Vector3d pos0 { .2,-1.0,.84};
		Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
		R0.row(2) = -pos0.normalized();
		R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
		R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
		R0.transposeInPlace();
		camera->setPosition(pos0.data());
		camera->setRotMatrix(R0.data());

		ioUsers.push_back(camera);
		renderState.camera = camera;


		// RtCfg cfg { "/data/gearth/dc2/" };
		// RtCfg cfg { "/data/gearth/dc3/" };
		// RtCfg cfg { "/data/gearth/nyc/" };
		// RtCfg cfg { "/data/gearth/tampa/" };
		RtCfg cfg { "/data/gearth/many3/" };

		rtr = std::make_shared<RtRenderer>(cfg, this);
		rtr->init();
	}

	inline virtual void doRender(RenderState& rs) override {

		auto cmd = *rs.frameData->cmd;

		vk::CommandBufferBeginInfo beginInfo { {}, {} };
		vk::Rect2D aoi { { 0, 0 }, { windowWidth, windowHeight } };
		vk::ClearValue clears_[2] = {
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.05f,.1f,1.f } } }, // color
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 1.f,1.f,1.f,1.f } } }  // depth
		};
		vk::RenderPassBeginInfo rpInfo {
			*simpleRenderPass.pass, *simpleRenderPass.framebuffers[rs.frameData->scIndx],
				aoi, {2, clears_}
		};
		cmd.reset();
		cmd.begin(beginInfo);
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);


		// Render scene
		rtr->stepAndRender(renderState, cmd);
		std::vector<vk::CommandBuffer> cmds = {
			cmd
		};

		cmd.endRenderPass();
		cmd.end();

		auto& fd = *rs.frameData;
		vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eAllGraphics;
		vk::SubmitInfo submitInfo {
			1, &(*fd.scAcquireSema), // wait sema
			&waitMask,
			//1, &(*commandBuffers[fd.scIndx]),
			(uint32_t)cmds.size(), cmds.data(),
			1, &*fd.renderCompleteSema // signal sema
		};
		queueGfx.submit(submitInfo, *fd.frameDoneFence);


		// TODO dsiable
		deviceGpu.waitForFences({*fd.frameDoneFence}, true, 999999999);
	}

	inline ~RtApp() {
		for (auto& fd : frameDatas)
			deviceGpu.waitForFences({*fd.frameDoneFence}, true, 999999999);
	}
};


int main() {

	RtApp app;
	app.windowWidth = 1000;
	app.windowHeight = 800;

	app.initVk();

	while (not app.isDone()) {
		if (not app.headless)
			bool proc = app.pollWindowEvents();
		//if (proc) app.render();
		app.render();
		//usleep(33'000);
	}

	return 0;
}
