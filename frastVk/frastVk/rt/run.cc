#include "frastVk/utils/eigen.h"

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
		pos0 *= .8;
		Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
		R0.row(2) = -pos0.normalized();
		R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
		R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
		R0.transposeInPlace();
		camera->setPosition(pos0.data());
		camera->setRotMatrix(R0.data());

		ioUsers.push_back(camera.get());
		renderState.camera = camera.get();


		// RtCfg cfg { "/data/gearth/dc2/" };
		// RtCfg cfg { "/data/gearth/dc3/" };
		// RtCfg cfg { "/data/gearth/nyc/" };
		// RtCfg cfg { "/data/gearth/tampa/" };
		// RtCfg cfg { "/data/gearth/many3/" };
		RtCfg cfg { "/data/gearth/many3_wgs/" };

		rtr = std::make_shared<RtRenderer>(cfg, this);
		rtr->init();
	}

	inline void setCasterFakeData(RenderState& rs) {
		FrameData& fd = *rs.frameData;

			Image casterImage { 256, 256, Image::Format::RGBA };
			casterImage.alloc();
			for (int y=0; y<256; y++) {
				int yy = y + fd.n;
				for (int x=0; x<256; x++) {
					casterImage.buffer[y*256*4+x*4 +0] = (((x/32) + (yy/32))%2 == 0) ? 240 : 130;
					casterImage.buffer[y*256*4+x*4 +1] = (((x/32) + (yy/32))%2 == 0) ? 240 : 130;
					casterImage.buffer[y*256*4+x*4 +2] = (((x/32) + (yy/32))%2 == 0) ? 240 : 130;
					casterImage.buffer[y*256*4+x*4 +3] = 255;
				}
			}

			CasterWaitingData cwd;
			alignas(16) float matrix1[16];
			alignas(16) float matrix2[16];

			if(0) {
				// Use view camera
				for (int i=0;i<16;i++) matrix1[i] = (i%5) == 0;
				rs.mvpf(matrix1);
				cwd.setMask(1u);
				cwd.setMatrix1(matrix1);
			} else {
				// Use a camera inside aoi
				CameraSpec camSpec { (float)256, (float)256, 22 * 3.141 / 180. };
				SphericalEarthMovingCamera tmpCam(camSpec);
				// Eigen::Vector3d pos0 {0.171211, -0.756474,  0.630934};
				// Eigen::Vector3d pos0 { 0.174588, -0.757484,  0.627551 };
				Eigen::Vector3d pos0 { 0.174582, -0.757376,  0.627687 };
				Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
				R0.row(2) = -pos0.normalized();
				R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
				R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
				R0.transposeInPlace();
				tmpCam.setPosition(pos0.data());
				tmpCam.setRotMatrix(R0.data());
				Eigen::Map<const Eigen::Matrix<double,4,4,Eigen::RowMajor>> view (tmpCam.view());
				Eigen::Map<const Eigen::Matrix<double,4,4,Eigen::RowMajor>> proj (tmpCam.proj());
				Eigen::Matrix<float,4,4,Eigen::RowMajor> m = (proj * view).cast<float>();
				memcpy(matrix1, m.data(), 4*16);
				cwd.setMatrix1(matrix1);

				{
					pos0 = Vector3d {0.173375,  -0.7557, 0.631619};
					pos0 = Vector3d { 0.174588, -0.757484,  0.627551 };
					tmpCam.setPosition(pos0.data());
					Eigen::Map<const Eigen::Matrix<double,4,4,Eigen::RowMajor>> view (tmpCam.view());
					Eigen::Map<const Eigen::Matrix<double,4,4,Eigen::RowMajor>> proj (tmpCam.proj());
					Eigen::Matrix<float,4,4,Eigen::RowMajor> m = (proj * view).cast<float>();
					memcpy(matrix2, m.data(), 4*16);
					cwd.setMatrix2(matrix2);
				}
				cwd.setMask(3u);
				// cwd.setMask(1u);
			}
			// fmt::print(" - Have caster matrix:\n{}\n", Eigen::Map<const Eigen::Matrix<float,4,4,Eigen::RowMajor>>{matrix1});

			cwd.setImage(casterImage);

			rtr->setCasterInRenderThread(cwd,this);
	}

	inline virtual std::vector<vk::CommandBuffer> doRender(RenderState& rs) override {


		auto cmd = *rs.frameData->cmd;

		setCasterFakeData(rs);


		vk::Rect2D aoi { { 0, 0 }, { windowWidth, windowHeight } };
		vk::ClearValue clears_[2] = {
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 0.f,0.05f,.1f,1.f } } }, // color
			vk::ClearValue { vk::ClearColorValue { std::array<float,4>{ 1.f,1.f,1.f,1.f } } }  // depth
		};
		vk::RenderPassBeginInfo rpInfo {
			*simpleRenderPass.pass, *simpleRenderPass.framebuffers[rs.frameData->scIndx],
				aoi, {2, clears_}
		};
		cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);
		// cmd.nextSubpass(vk::SubpassContents::eInline);


		// Render scene
		rtr->stepAndRender(renderState, cmd);
		std::vector<vk::CommandBuffer> cmds = {
			cmd
		};

		cmd.endRenderPass();
		// cmd.end();

		return cmds;
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
		// if (not app.headless) bool proc = app.pollWindowEvents();
		//if (proc) app.render();
		app.render();
		//usleep(33'000);
	}

	return 0;
}
