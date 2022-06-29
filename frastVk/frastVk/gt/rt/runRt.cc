// #include "frastVk/gt/gt.h"

#include <fmt/ostream.h>

#include "frastVk/gt/rt/rt.h"
#include "frastVk/core/fvkApi.h"
#include "frastVk/core/fvkShaders.h"

#include "frastVk/utils/eigen.h"


struct TestRtApp : public BaseApp {

	RtRenderer renderer;

	inline TestRtApp(const AppConfig& cfg) : BaseApp(cfg) {
	}

	inline void update() {
		GtUpdateContext<RtTypes> gtuc { renderer.loader, *renderer.obbMap, renderer.gtpd };
		gtuc.sseThresholdClose = .9;
		gtuc.sseThresholdOpen = 1.5;
		gtuc.two_tan_half_fov_y = 1.;
		gtuc.wh.setConstant(cfg.width);

		//gtuc.mvp.setIdentity();
		// renderState.mvpf(gtuc.mvp.data());

		MatrixStack mstack;
		mstack.push(camera->proj());
		mstack.push(camera->view());
		double* mvpd = mstack.peek();
		for (int i=0; i<16; i++) gtuc.mvp.data()[i] = static_cast<float>(mvpd[i]);
		// fmt::print(" - MVP Matrix:\n{}\n", gtuc.mvp);

		gtuc.zplus = decltype(gtuc.zplus)::UnitZ();

		renderer.update(gtuc);
	}

	inline virtual void initVk() override {
		BaseApp::initVk();

		CameraSpec spec { (double)windowWidth, (double)windowHeight, 45. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);

		Eigen::Vector3d pos0 { 0.136273, -1.03348, 0.552593 };
		Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
		R0.row(2) = -pos0.normalized();
		R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
		R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
		R0.transposeInPlace();
		camera->setPosition(pos0.data());
		camera->setRotMatrix(R0.data());

		if (glfwWindow) glfwWindow->ioUsers.push_back(camera.get());
		renderState.camera = camera.get();

		renderer.init(mainDevice, dpool, simpleRenderPass, cfg);
	}

	inline virtual void doRender(RenderState& rs) override {
		auto &fd = *rs.frameData;
		auto &cmd = fd.cmd;

		fd.cmd.begin();
		simpleRenderPass.begin(fd.cmd, fd);
		renderer.render(rs, fd.cmd);
		simpleRenderPass.end(fd.cmd, fd);
		// fd.cmd.clearImage(*fd.swapchainImg, {(float)(fd.n%3==0), (float)(fd.n%3==1), (float)(fd.n%3==2), 1.f});

		if (fd.n % 60 == 0)
			fmt::print(" - Rendering frame {} with {} active tiles\n", fd.n, renderer.activeTiles());
	}
};


int main() {



	/*
	for (int i=0; i<100; i++) {
		fmt::print(" (update)\n");
		GtUpdateContext<RtTypes> gtuc { renderer.loader, *renderer.obbMap, renderer.gtpd };
		gtuc.sseThresholdClose = .9;
		gtuc.sseThresholdOpen = 1.5;
		gtuc.two_tan_half_fov_y = 1.;
		gtuc.wh.setConstant(512);
		gtuc.mvp.setIdentity();
		gtuc.zplus = decltype(gtuc.zplus)::UnitZ();
		renderer.update(gtuc);
		sleep(1);


		fmt::print(" (render)\n");
		RenderState rs;
		Command cmd;
		// vk::raii::CommandBuffer cmd_ {nullptr};
		// auto cmd = *cmd_;
		renderer.render(rs, cmd);
	}
	*/

	AppConfig cfg;
	cfg.width = 1024;
	cfg.height = 1024;
	TestRtApp app{cfg};
	app.initVk();


	// for (int i=0; i<1; i++) {
	for (int i=0; i<1000; i++) {
		app.update();
		app.render();
		usleep(100'000);

	}

	return 0;
}
