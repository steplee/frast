#include <fmt/ostream.h>
#include <fmt/color.h>

#include "frastVk/gt/ftr/ftr.h"
#include "frastVk/core/fvkApi.h"
#include "frastVk/core/fvkShaders.h"

#include "frastVk/utils/eigen.h"


struct TestFtrApp : public BaseApp {

	FtRenderer renderer;

	inline TestFtrApp(const AppConfig& cfg)
		: BaseApp(cfg),
		  renderer(FtTypes::Config {
				.obbIndexPath = "/data/naip/mocoNaip/index.v1.bin",
				.colorDsetPath = "/data/naip/mocoNaip/out.ft",
				.elevDsetPath = "/data/elevation/srtm/usa.11.ft"
		  })
	{
	}

	inline void update() {
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

		renderer.init(mainDevice, dpool, simpleRenderPass, mainQueue, frameDatas[0].cmd, cfg);

		float color[4] = {.1f,0.f,0.f,.1f};
		Image tstImg { 512,512,Image::Format::RGBA };
		tstImg.alloc();
		for (int y=0; y<512; y++)
		for (int x=0; x<512; x++) {
			uint8_t c = ((y/16)%2) == 1+((x/16)%2) ? 250 : 100;
			tstImg.buffer[y*512*4+x*4+0] = tstImg.buffer[y*512*4+x*4+1] = tstImg.buffer[y*512*4+x*4+2] = c;
			tstImg.buffer[y*512*4+x*4+3] = 200;
		}
		renderer.cwd.setImage(tstImg);
		renderer.cwd.setColor1(color);
		renderer.cwd.setMask(0b01);
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

		fd.cmd.begin();
		simpleRenderPass.begin(fd.cmd, fd);
		renderer.render(rs, fd.cmd);
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
};


int main() {

	AppConfig cfg;
	cfg.width = 1024;
	cfg.height = 1024;
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

