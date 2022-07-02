// #include "frastVk/gt/gt.h"

#include <fmt/ostream.h>

#include "frastVk/gt/rt/rt.h"
#include "frastVk/core/fvkApi.h"
#include "frastVk/core/fvkShaders.h"

#include "frastVk/utils/eigen.h"

#include "frastVk/extra/frustum/frustum.h"
#include "frastVk/extra/particleCloud/particleCloud.h"
#include "frastVk/extra/text/textSet.h"
#include "frastVk/extra/primitives/earthEllipsoid.h"


struct TestRtApp : public BaseApp {

	RtRenderer renderer;
	std::shared_ptr<FrustumSet> frustumSet;
	std::shared_ptr<ParticleCloudRenderer> pset;
	std::shared_ptr<SimpleTextSet> textSet;
	std::shared_ptr<EarthEllipsoid> earthEllipsoid;

	bool moveCaster = true;

	inline TestRtApp(const AppConfig& cfg)
		: BaseApp(cfg),
		  renderer(RtTypes::Config {
				// .obbIndexPath = "/data/gearth/tpAois_wgs/index.v1.bin",
				// .rootDir = "/data/gearth/tpAois_wgs"
				.obbIndexPath = "/data/gearth/many3_wgs/index.v1.bin",
				.rootDir = "/data/gearth/many3_wgs"
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

		GtUpdateContext<RtTypes> gtuc { renderer.loader, *renderer.obbMap, renderer.gtpd, gtucd };
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

		float color[4] = {.1f,0.f,.1f,.1f};
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
		renderer.cwd.setColor2(color);
		renderer.cwd.setMask(0b01);

		if (1) {
			frustumSet = std::make_shared<FrustumSet>(this, 2);
			float f_color[4] = {1.f,0.f,1.f,1.f};
			frustumSet->setIntrin(0, 512,512,512,512);
			frustumSet->setColor(0, f_color);
			Vector3d fpos0 {0.115938, -0.879261  ,0.468235 };
			for (int i=0; i<10; i++) {
				Vector3d pos = fpos0 + Vector3d{1.,0.,0.} * ((i/10.)/100.);
				frustumSet->setPose(0, pos,R0,true);
			}
		}

		if (1) {
			pset = std::make_shared<ParticleCloudRenderer>(this, 1<<20);
			auto N_PARTS = 1<<10;
			std::vector<float> ps(4*(N_PARTS), 0.f);
			for (int i=0; i<N_PARTS; i++) {
				ps[i*4+0] = ((rand() % 9999999) / 9999999.f) * 2.f - 1.f;
				ps[i*4+1] = ((rand() % 9999999) / 9999999.f) * 2.f - 1.f;
				ps[i*4+2] = ((rand() % 9999999) / 9999999.f) * 2.f - 1.f;
				ps[i*4+3] = (rand() % 9999) / 9999.f;
			}
			pset->uploadParticles(ps, true);
		}

		if (1)
			textSet = std::make_shared<SimpleTextSet>(this);

		if (1)
			earthEllipsoid = std::make_shared<EarthEllipsoid>(this);

	}

	inline virtual void doRender(RenderState& rs) override {
		auto &fd = *rs.frameData;
		auto &cmd = fd.cmd;

		RowMatrix4f mvpf;
		rs.mvpf(mvpf.data());

		if (moveCaster) {
			Eigen::Map<const RowMatrix4d> view { camera->view() };
			Eigen::Map<const RowMatrix4d> proj { camera->proj() };
			RowMatrix4f matrix = (proj*view).cast<float>();
			renderer.cwd.setMatrix1(matrix.data());
		}

		if (textSet) {
			constexpr double R1         = (6378137.0);
			RowMatrix4f model { RowMatrix4f::Identity() };
			Vector3f p {0.116107 ,-0.876376  ,0.465961};
			model.topRightCorner<3,1>() = p;
			model.block<3,1>(0,2) = -model.topRightCorner<3,1>();
			model.block<3,1>(0,0) =  model.block<3,1>(0,2).cross(Eigen::Vector3f::UnitZ()).normalized();
			model.block<3,1>(0,1) = model.block<3,1>(0,2).cross(model.block<3,1>(0,0)).normalized();
			// float scale = static_cast<float>(R1)*1.85f * (sqrt(1.f+.01f*(p-eye).norm())-1.f) + 19.0;
			// float scale = static_cast<float>(R1)*1.85f * (sqrt(1.f+.01f*(p-eye).norm())-1.f) + 19.0;
			model.topLeftCorner<3,3>() *= 120.f / static_cast<float>(R1);

			float color[4] = {1.f,1.f,.5f,1.f};
			textSet->setText(0, "hello world", model.data(), color);
			textSet->setAreaAndSize(0,0, windowWidth, windowHeight, 1.f, mvpf.data());
		}

		fd.cmd.begin();

		simpleRenderPass.begin(fd.cmd, fd);

		renderer.render(rs, fd.cmd);

		if (frustumSet) frustumSet->render(rs, fd.cmd);
		if (textSet) textSet->render(rs,fd.cmd);
		if (earthEllipsoid) earthEllipsoid->render(rs,fd.cmd);

		simpleRenderPass.end(fd.cmd, fd);

		if (pset) pset->render(rs, fd.cmd, simpleRenderPass.framebuffers[fd.scIndx]);
		// void render(RenderState& rs, Command& cmd, VkFramebuffer outputFramebuffer);
		// void uploadParticles(std::vector<float>& particles, bool normalizeMaxInplace=true, float divisor=1.f);

		// fd.cmd.clearImage(*fd.swapchainImg, {(float)(fd.n%3==0), (float)(fd.n%3==1), (float)(fd.n%3==2), 1.f});

		if (fd.n % 60 == 0)
			fmt::print(" - Rendering frame {} with {} active tiles\n", fd.n, renderer.activeTiles());
	}

	inline virtual bool handleKey(int key, int scancode, int action, int mods) override {
		if (action == GLFW_PRESS and key == GLFW_KEY_U) {
			bool updateAllowed = renderer.toggleUpdateAllowed();
			fmt::print(fmt::fg(fmt::color::light_green), " - Toggling update allowed to {}\n", updateAllowed);
		}
		if (action == GLFW_PRESS and key == GLFW_KEY_M) {
			moveCaster = !moveCaster;
			fmt::print(fmt::fg(fmt::color::light_green), " - Toggling moveCaster to {}\n", moveCaster);
		}
		if (action == GLFW_PRESS and key == GLFW_KEY_B) {
			renderer.debugMode = !renderer.debugMode;
			fmt::print(fmt::fg(fmt::color::light_green), " - Toggling debug mode to {}\n", renderer.debugMode);
		}
		return BaseApp::handleKey(key,scancode,action,mods);
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
	for (int i=0; i<100000; i++) {
		app.update();
		app.render();
		usleep(10'000);

	}

	return 0;
}
