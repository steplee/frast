
#include "vk/app.h"

#include "vk/clipmap1/clipmap1.h"
#include "vk/tiled_renderer/tiled_renderer.h"

struct GlobeApp : public VkApp {

		std::shared_ptr<ClipMapRenderer1> clipmap;
		std::shared_ptr<TiledRenderer> tiledRenderer;

	inline virtual void init() override {
		VkApp::init();
		//set position of camera offset by loaded mld ctr

		CameraSpec spec { (float)windowWidth, (float)windowHeight, 45 * 3.141 / 180. };
		camera = std::make_shared<MovingCamera>(spec);
		//alignas(16) float pos0[] { 0, 0, .9999f };
		//alignas(16) float pos0[] { 0, 0, (float)(2.0 * 2.38418579e-7) };
		alignas(16) double pos0[] { 
			(double)(-8590834.045999 / 20037508.342789248), (float)(4757669.951554 / 20037508.342789248),
			//0,0,
			//(double)(2.0 * 2.38418579e-7) };
			//(double)(2.0 * 1./(1<<(18-1))) };
			// (double)(2.0 * 1./(1<<(10-1))) };
			(double)(2.0 * 1./(1<<(7-1))) };
		alignas(16) double R0[] {
			1,0,0,
			0,-1,0,
			0,0,-1 };
		camera->setPosition(pos0);
		camera->setRotMatrix(R0);
		ioUsers.push_back(camera);
		renderState.camera = camera;

		//clipmap = std::make_shared<ClipMapRenderer1>(this);
		//clipmap->init();
		tiledRenderer = std::make_shared<TiledRenderer>(this);
		tiledRenderer->init();
	}

	inline virtual void doRender(RenderState& rs) override {

		std::vector<vk::CommandBuffer> cmds = {
			tiledRenderer->stepAndRender(renderState)
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
	}
};


int main() {

	GlobeApp app;
	app.windowWidth = 1000;
	app.windowHeight = 800;
	// app.headless = true;

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
