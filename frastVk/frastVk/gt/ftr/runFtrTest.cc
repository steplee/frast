#include <fmt/ostream.h>
#include <fmt/color.h>

#include "frastVk/gt/ftr/ftr.h"
#include "frastVk/core/fvkApi.h"
#include "frastVk/core/fvkShaders.h"
#include "frastVk/core/imgui_app.h"

#include "frastVk/utils/eigen.h"
#include "frastVk/extra/primitives/earthEllipsoid.h"
#include "frastVk/extra/headlessCopyHelper.hpp"

// using Super = BaseApp;
using Super = ImguiApp;

// struct TestFtrApp : public HeadlessCopyMixin<Super> {
struct TestFtrApp : public Super, public HeadlessCopyMixin<TestFtrApp> {

	FtRenderer renderer;
	std::shared_ptr<EarthEllipsoid> earthEllipsoid;

	inline TestFtrApp(const AppConfig& cfg)
		// : HeadlessCopyMixin<Super>(cfg),
		: Super(cfg),
		  renderer(FtTypes::Config {
				.debugMode = true,
				.obbIndexPaths = {"/data/naip/tampa/index.v1.bin", "/data/naip/mocoNaip/index.v1.bin"},
				.colorDsetPaths = {"/data/naip/tampa/tampaAoi.ft", "/data/naip/mocoNaip/out.ft"},
				// .obbIndexPaths = {"/data/naip/tampa/index.v1.bin"},
				// .colorDsetPaths = {"/data/naip/tampa/tampaAoi.ft"},
				// .obbIndexPaths = { "/data/naip/mocoNaip/index.v1.bin"},
				// .colorDsetPaths = { "/data/naip/mocoNaip/out.ft"},
				.elevDsetPath = "/data/elevation/srtm/usa.11.ft"
		  })
	{
	}

	inline void update() {
		/*
		GtUpdateCameraData gtucd;
		gtucd.two_tan_half_fov_y = 1.;
		gtucd.wh.setConstant(cfg.width);


		//gtucd.mvp.setIdentity();
		// renderState.mvpf(gtuc.mvp.data());

		MatrixStack mstack;
		mstack.push(camera->proj());
		mstack.push(camera->view());
		double* mvpd = mstack.peek();
		for (int i=0; i<16; i++) gtucd.mvp.data()[i] = static_cast<float>(mvpd[i]);
		// fmt::print(" - MVP Matrix:\n{}\n", gtucd.mvp);


		RowMatrix4f imvp = gtucd.mvp.inverse();
		RowMatrix84f corners;
		for (int i=0; i<8; i++) corners.row(i) << (float)((i%4)==1 or (i%4)==2), (i%4)>=2, (i/4), 1.f;
		gtucd.frustumCorners = (corners * imvp.transpose()).rowwise().hnormalized();

		gtucd.zplus = decltype(gtucd.zplus)::UnitZ();
		const double* viewInv = camera->viewInv();
		gtucd.eye = Vector3f { viewInv[0*4+3], viewInv[1*4+3], viewInv[2*4+3] };

		GtUpdateContext<FtTypes> gtuc { renderer.loader, *renderer.obbMap, renderer.gtpd, gtucd };
		gtuc.sseThresholdClose = .9;
		gtuc.sseThresholdOpen = 1.5;
		renderer.update(gtuc);
		*/

		renderer.defaultUpdate(camera.get());
	}

	inline virtual void initVk() override {
		Super::initVk();

		CameraSpec spec { (double)windowWidth, (double)windowHeight, 40*M_PI/180. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);

		Eigen::Vector3d pos0 { 0.136273, -1.03348, 0.552593 };
		Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
		R0.row(2) = -pos0.normalized();
		R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
		R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
		R0.transposeInPlace();
		camera->setPosition(pos0.data());
		camera->setRotMatrix(R0.data());

		window->addIoUser(camera.get());
		renderState.camera = camera.get();

		renderer.init(mainDevice, dpool, simpleRenderPass, mainQueue, frameDatas[0].cmd, cfg);

		float color[4] = {.1f,0.f,0.f,.1f};
		Image tstImg { 512,512,Image::Format::RGBA };
		tstImg.alloc();
		for (int y=0; y<512; y++)
		for (int x=0; x<512; x++) {
			uint8_t c = (y % 16 == 0 and x % 16 == 0) ? 200 : 0;
			tstImg.buffer[y*512*4+x*4+0] = tstImg.buffer[y*512*4+x*4+1] = tstImg.buffer[y*512*4+x*4+2] = c;
			tstImg.buffer[y*512*4+x*4+3] = 200;
		}
		renderer.cwd.setImage(tstImg);
		renderer.cwd.setColor1(color);
		renderer.cwd.setMask(0b01);

		if (1) earthEllipsoid = std::make_shared<EarthEllipsoid>(this);

		initHeadlessCopyMixin();
	}

	inline virtual void doRender(RenderState& rs) override {
		auto &fd = *rs.frameData;
		auto &cmd = fd.cmd;

		{
			Eigen::Map<const RowMatrix4d> view { camera->view() };
			Eigen::Map<const RowMatrix4d> proj { camera->proj() };
			RowMatrix4f matrix = (proj*view).cast<float>();
			renderer.cwd.setMatrix1(matrix.data());
		}

		simpleRenderPass.begin(fd.cmd, fd);
		renderer.render(rs, fd.cmd);
		if (earthEllipsoid) earthEllipsoid->render(rs,fd.cmd);
		simpleRenderPass.end(fd.cmd, fd);
		// fd.cmd.clearImage(*fd.swapchainImg, {(float)(fd.n%3==0), (float)(fd.n%3==1), (float)(fd.n%3==2), 1.f});

		if (fd.n % 60 == 0)
			fmt::print(" - Rendering frame {} with {} active tiles\n", fd.n, renderer.activeTiles());
	}


	inline virtual bool handleKey(int key, int scancode, int action, int mods) override {
		if (action == GLFW_PRESS and key == GLFW_KEY_U) {
			bool updateAllowed = renderer.toggleUpdateAllowed();
			fmt::print(fmt::fg(fmt::color::light_green), " - Toggling update allowed to {}\n", updateAllowed);
		}
		return BaseApp::handleKey(key,scancode,action,mods);
	}

	inline virtual void handleCompletedHeadlessRender(RenderState& rs, FrameData& fd) override {
		if (window->headless()) {
			helper_handleCompletedHeadlessRender(rs,fd, &simpleRenderPass.depthImages[fd.scIndx]);
			/*
			Submission submission { DeviceQueueSpec{mainDevice,mainQueue} };
			// submission.fence = nullptr;
			submission.fence = fd.frameAvailableFence;
			// submission.signalSemas = { headlessCopyDoneSema };
			submission.waitSemas = { fd.renderCompleteSema };
			submission.waitStages = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT };
			// submission.submit(&fd.cmd.cmdBuf, 1, false); // Do NOT block on fence, do NOT reset fence -- that way frameAvailableFence won't be set until we read it in acquireNextFrame()!!!
			submission.submit(&fd.cmd.cmdBuf, 1);
			*/
			fmt::print(" - handled done\n");
		}
	}

};


int main(int argc, char** argv) {



	AppConfig cfg;
	cfg.width = 1600;
	cfg.height = 1000;

	cfg.windowSys = AppConfig::WindowSys::eGlfw;
	for (int i=0; i<argc; i++)
		if (strcmp(argv[i], "-h") == 0 or strcmp(argv[i], "--headless") == 0)
			cfg.windowSys = AppConfig::WindowSys::eHeadless;

	TestFtrApp app{cfg};
	app.initVk();


	// for (int i=0; i<1; i++) {
	for (int i=0; i<100000; i++) {
		app.update();
		app.render();
		usleep(10'000);

	}

	return 0;
}

