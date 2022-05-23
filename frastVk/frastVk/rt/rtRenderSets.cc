#include "frastVk/utils/eigen.h"

#include "core/app.h"

#include "rt/rt.h"

#include "thirdparty/nlohmann/json.hpp"

#include <fmt/ostream.h>

using namespace	nlohmann;

using namespace rt;

using RowMatrix34d = Eigen::Matrix<double,3,4,Eigen::RowMajor>;

struct RtApp : public VkApp {

		std::shared_ptr<RtRenderer> rtr;
		json jobj;

		bool advance = false;
		

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override {
			VkApp::handleKey(key,scancode,action,mods);
			if (key == GLFW_KEY_U and action == GLFW_PRESS) {
				if (rtr) {
					rtr->allowUpdate = !rtr->allowUpdate;
					fmt::print(" - Setting allowUpdate: {}\n", rtr->allowUpdate);
				}
			}
			if (key == GLFW_KEY_N and action == GLFW_PRESS) {
				advance = true;
			}
			return false;
		}


	inline virtual void initVk() override {
		VkApp::initVk();

		CameraSpec spec { (float)windowWidth, (float)windowHeight, 45 * 3.141 / 180. };
		camera = std::make_shared<SphericalEarthMovingCamera>(spec);
		Eigen::Vector3d pos0 { 0.116664 ,-0.884764  ,0.473077};
		Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
		R0.row(2) = -pos0.normalized();
		R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
		R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
		R0.transposeInPlace();
		camera->setPosition(pos0.data());
		camera->setRotMatrix(R0.data());

		ioUsers.push_back(camera.get());
		renderState.camera = camera.get();

		std::string root = jobj["srcDir"];
		RtCfg cfg { root };
		cfg.sseThresholdOpen = 1.0;
		cfg.sseThresholdClose = .5;

		rtr = std::make_shared<RtRenderer>(cfg, this);
		rtr->init();
	}

	inline virtual std::vector<vk::CommandBuffer> doRender(RenderState& rs) override {


		auto cmd = *rs.frameData->cmd;


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

		// Render scene
		rtr->stepAndRender(renderState, cmd);
		std::vector<vk::CommandBuffer> cmds = {
			cmd
		};

		cmd.endRenderPass();

		return cmds;
	}

	inline ~RtApp() {
		for (auto& fd : frameDatas)
			deviceGpu.waitForFences({*fd.frameDoneFence}, true, 999999999);
	}
};


int main(int argc, char** argv) {

	RtApp app;
	app.windowWidth = 1000;
	app.windowHeight = 800;
	app.windowWidth = app.windowHeight = 512;
	// app.windowWidth = app.windowHeight = 1024;

	std::ifstream ifs(argv[1]);
	json jobj = json::parse(ifs);
	auto &entries = jobj["entries"];
	app.jobj = jobj;

	app.initVk();

	app.rtr->setDataLoaderSleepMicros(5'000);


		int i=0;
		for (auto ent : entries) {
			int j =0;
			std::vector<double> camera6 = ent["camera"];
			std::vector<double> base = ent["base"];
			std::vector<std::vector<double>> perturbedPoses = ent["perturbed"];
			std::vector<std::vector<double>> logDiffs = ent["logDiffs"];

			std::vector<std::vector<double>> poses = {base};
			for (auto& pp: perturbedPoses) poses.push_back(pp);

			double hfov = 2.0f * std::atan(.5f * camera6[0] / camera6[2]);
			double vfov = 2.0f * std::atan(.5f * camera6[1] / camera6[3]);
			CameraSpec cs { camera6[0], camera6[1], hfov, vfov };

			fmt::print(" - Camera {} {} :: {} {}\n", cs.w, cs.h, cs.fx(), cs.fy());

			for (auto &pose : poses) {
				Eigen::Map<RowMatrix34d> p { pose.data() };
				Vector3d pos = p.topRightCorner<3,1>();
				RowMatrix3d R = p.topLeftCorner<3,3>();
				app.camera->setSpec(cs);
				app.camera->setPosition(pos.data());
				app.camera->setRotMatrix(R.data());
				fmt::print(" - on {}:{} at {}\n", i, j, pos.transpose());

				for (int k=0; k<40000; k++) {
					app.render();
					usleep(10'000);
					if (app.advance) {
						app.advance = false;
						break;
					}
				}
				j++;
			}
			i++;
	}

	return 0;
}

