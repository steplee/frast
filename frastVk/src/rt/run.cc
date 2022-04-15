
#include "vk/app.h"

#include "rt/rt.h"

using namespace rt;

struct RtApp : public VkApp {

		std::shared_ptr<RtRenderer> rtr;

	inline virtual void init() override {
		VkApp::init();
		//set position of camera offset by loaded mld ctr

		CameraSpec spec { (float)windowWidth, (float)windowHeight, 45 * 3.141 / 180. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);
		alignas(16) double pos0[] { 0,-2.0,0 };
		alignas(16) double R0[] {
			1,0,0,
			0,0,1,
			0,-1,0 };
		camera->setPosition(pos0);
		camera->setRotMatrix(R0);
		ioUsers.push_back(camera);
		renderState.camera = camera;

		// RtCfg cfg { "/data/gearth/dc2/" };
		// RtCfg cfg { "/data/gearth/dc3/" };
		// RtCfg cfg { "/data/gearth/nyc/" };
		// RtCfg cfg { "/data/gearth/tampa/" };
		RtCfg cfg { "/data/gearth/many/" };

		rtr = std::make_shared<RtRenderer>(cfg, this);
		rtr->init();
	}

	inline virtual void doRender(RenderState& rs) override {

		std::vector<vk::CommandBuffer> cmds = {
			rtr->stepAndRender(renderState)
		};

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

	app.init();

	while (not app.isDone()) {
		if (not app.headless)
			bool proc = app.pollWindowEvents();
		//if (proc) app.render();
		app.render();
		//usleep(33'000);
	}

	return 0;
}
