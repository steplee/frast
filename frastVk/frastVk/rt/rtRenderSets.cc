#include "frastVk/utils/eigen.h"

#include "core/app.h"

#include "rt/rt.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "rt/decode/stb_image_write.h"

#include "thirdparty/nlohmann/json.hpp"

#include <fmt/ostream.h>
#include <iostream>

#include <sys/types.h>
#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>

using namespace	nlohmann;

using namespace rt;

using RowMatrix34d = Eigen::Matrix<double,3,4,Eigen::RowMajor>;
using namespace Eigen;

struct SetCamera : public Camera {
	inline SetCamera() {
		Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
		Map<Matrix<double,4,4,RowMajor>> view ( view_ );

		quat_[0] = quat_[1] = quat_[2] = 0;
		quat_[3] = 1;
		viewInv.setIdentity();
		view.setIdentity();
		recompute_view();
	}
	inline virtual ~SetCamera() {}

	inline void recompute_view() {
		Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
		Map<Matrix<double,4,4,RowMajor>> view ( view_ );
		view.topLeftCorner<3,3>() = viewInv.topLeftCorner<3,3>().transpose();
		view.topRightCorner<3,1>() = -(viewInv.topLeftCorner<3,3>().transpose() * viewInv.topRightCorner<3,1>());
	}

	inline virtual void setPosition(double* t) override {
		viewInv_[0*4+3] = t[0];
		viewInv_[1*4+3] = t[1];
		viewInv_[2*4+3] = t[2];
		// maybe_set_near_far();
		// recompute_view();
	}
	inline virtual void setRotMatrix(double* R) override {
		Map<const Matrix<double,3,3,RowMajor>> RR ( R );
		Eigen::Quaterniond q { RR };
		quat_[0] = q.x(); quat_[1] = q.y(); quat_[2] = q.z(); quat_[3] = q.w();
		Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
		viewInv.topLeftCorner<3,3>() = q.toRotationMatrix();
		// recompute_view();
	}
	

	inline virtual void step(double dt) override {
		Map<Matrix<double,3,1>> vel { vel_ };
		Map<Matrix<double,3,1>> acc { acc_ };
		Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
		Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };

		static int __updates = 0;
		__updates++;

		double r = viewInv.topRightCorner<3,1>().norm();
		// double d = r - 1.0;
		double d = r - .9966;

		//constexpr float SPEED = 5.f;
		// double SPEED = .00000000000001f + 5.f * std::fabs(viewInv(2,3));
		drag_ = 15.;
		double SPEED = .0000000001 + 25.f * std::max(d, 3e-4);

		viewInv.topRightCorner<3,1>() += vel * dt + acc * dt * dt * .5 * SPEED;

		vel += acc * dt * SPEED;
		vel -= vel * (drag_ * dt);
		//if (vel.squaredNorm() < 1e-18) vel.setZero();
		acc.setZero();

		Vector3d pos = viewInv.topRightCorner<3,1>();
		Vector3d n = pos.normalized();
		quat = AngleAxisd(dquat_[0]*dt, n)
			* AngleAxisd(         0*dt, Vector3d::UnitZ()) * quat
			* AngleAxisd( dquat_[1]*dt, Vector3d::UnitX());

		// Make X axis normal to world
		/*
		if (pos.norm() < 1.2) {
			float speed = std::min(std::max(.1f - ((float)pos.norm() - 1.2f), 0.f), .1f);
			auto R = quat.toRotationMatrix();
			double angle = R.col(0).dot(n);
			quat = quat * AngleAxisd(angle*speed, n.normalized());
		}
		*/


		for (int i=0; i<4; i++) quat_[i] = quat.coeffs()(i);
		dquat_[0] = dquat_[1] = dquat_[2] = 0;

		quat = quat.normalized();
		viewInv.topLeftCorner<3,3>() = quat.toRotationMatrix();

		// maybe_set_near_far();
		recompute_view();
	}
	virtual bool handleKey(int key, int scancode, int action, int mods) override {
		bool isDown = action == GLFW_PRESS or action == GLFW_REPEAT;
		if (isDown) {
			Map<Matrix<double,3,1>> acc { acc_ };
			Map<const Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );

			Vector3d dacc { Vector3d::Zero() };
			if (isDown and key == GLFW_KEY_A) dacc(0) += -1;
			if (isDown and key == GLFW_KEY_D) dacc(0) +=  1;
			if (isDown and key == GLFW_KEY_W) dacc(2) +=  1;
			if (isDown and key == GLFW_KEY_S) dacc(2) += -1;
			if (isDown and key == GLFW_KEY_F) dacc(1) +=  1;
			if (isDown and key == GLFW_KEY_E) dacc(1) += -1;

			if (isDown and key == GLFW_KEY_I) {
				Map<Matrix<double,3,1>> vel { vel_ };
				Map<Matrix<double,3,1>> acc { acc_ };
				Map<Matrix<double,4,4,RowMajor>> viewInv ( viewInv_ );
				Eigen::Quaterniond quat { quat_[3], quat_[0], quat_[1], quat_[2] };
				std::cout << " [SetCamera::step()]\n";
				std::cout << "      - vel " << vel.transpose() << "\n";
				std::cout << "      - acc " << acc.transpose() << "\n";
				std::cout << "      - pos " << viewInv.topRightCorner<3,1>().transpose() << "\n";
				std::cout << "      - z+  " << viewInv.block<3,1>(0,2).transpose() << "\n";
				std::cout << "      - q   " << quat.coeffs().transpose() << "\n";
				std::cout << "      - n/f " << spec().near << " " << spec().far << "\n";
			}

			// Make accel happen local to frame
			acc += viewInv.topLeftCorner<3,3>() * dacc;
		}
		return false;
	}
	inline virtual bool handleMousePress(int button, int action, int mods) override {
		leftMouseDown = button == GLFW_MOUSE_BUTTON_LEFT and action == GLFW_PRESS;
		rightMouseDown = button == GLFW_MOUSE_BUTTON_RIGHT and action == GLFW_PRESS;
		return false;
	}
	inline virtual bool handleMouseMotion(double x, double y) override {
		Map<Matrix<double,3,1>> dquat { dquat_ };
		if (leftMouseDown and (lastX !=0 or lastY !=0)) {
			dquat(0) += (x - lastX) * .1f;
			dquat(1) += (y - lastY) * .1f;
		}
		lastX = x;
		lastY = y;
		return false;
	}

	protected:
		// void maybe_set_near_far();

		double drag_ = 2.9;
		alignas(16) double vel_[3] = {0};
		alignas(16) double acc_[3] = {0};
		alignas(16) double quat_[4];
		alignas(16) double dquat_[3] = {0};
		bool leftMouseDown = false, rightMouseDown = false;
		double lastX=0, lastY=0;

		double last_proj_r2 = 0;
};





struct RtApp : public VkApp {

		std::shared_ptr<RtRenderer> rtr;
		json jobj;

		bool advance = false;
		bool debug = false;
		bool readyToSave = true;
		ResidentImage finalImage, finalImageDepth;
		

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
		camera = std::make_shared<SetCamera>();
		camera->setSpec(spec);
		Eigen::Vector3d pos0 { 0.116664 ,-0.884764  ,0.473077};
		Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
		R0.row(2) = -pos0.normalized();
		R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
		R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
		R0.transposeInPlace();
		camera->setPosition(pos0.data());
		camera->setRotMatrix(R0.data());
		((SetCamera*)camera.get())->recompute_view();

		ioUsers.push_back(camera.get());
		renderState.camera = camera.get();

		std::string root = jobj["srcDir"];
		RtCfg cfg { root };
		cfg.sseThresholdOpen = 1.0;
		cfg.sseThresholdClose = .5;
		cfg.dbg = debug;

		finalImage.createAsCpuVisible(uploader, windowHeight, windowWidth, vk::Format::eR8G8B8A8Uint, nullptr);
		finalImageDepth.createAsDepthBuffer(uploader, windowHeight, windowWidth, true, vk::ImageUsageFlagBits::eTransferDst);

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

	inline bool done_updating() {
		int n_waiting = rtr->numWaitingAsks(false);
		// fmt::print(" - [app] n_waiting {}\n", n_waiting);
		return n_waiting == 0;
	}

	inline ~RtApp() {
		for (auto& fd : frameDatas)
			deviceGpu.waitForFences({*fd.frameDoneFence}, true, 999999999);
	}

	inline virtual void handleCompletedHeadlessRender(RenderState& rs, FrameData& fd) override {
		auto &srcImg = *sc.headlessImages[fd.scIndx];
		auto &depthImg = *simpleRenderPass.depthImages[fd.scIndx].image;
		auto& copyCmd = *sc.headlessCopyCmds[fd.scIndx];
		auto& fence = *sc.headlessCopyDoneFences[fd.scIndx];
		vk::Extent3D ex = vk::Extent3D{windowWidth,windowHeight,1};
		vk::Offset3D off{};

		if (readyToSave) {
			// fmt::print(" - Copying finalImage.\n");
			finalImage.copyFrom(copyCmd, srcImg, vk::ImageLayout::eColorAttachmentOptimal, *deviceGpu, *queueGfx, &fence, &*fd.renderCompleteSema, 0, ex);
			finalImageDepth.copyFrom(copyCmd, depthImg, vk::ImageLayout::eDepthStencilAttachmentOptimal, *deviceGpu, *queueGfx, &fence, 0, &*fd.scAcquireSema, ex, off, vk::ImageAspectFlagBits::eDepth);
		} else {

			// We still must signal scAcquireSema
			vk::PipelineStageFlags waitMasks[1] = {vk::PipelineStageFlagBits::eAllGraphics};
			vk::SubmitInfo submitInfo {
				{1u, &*fd.renderCompleteSema}, // wait sema
					{1u, waitMasks},
					{},
					{1u, &*fd.scAcquireSema} // signal sema
			};
			queueGfx.submit(submitInfo, fence);
			deviceGpu.waitForFences({1u, &fence}, true, 999999999999);
			deviceGpu.resetFences({1u, &fence});
		}

		/*
		if (1) {
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

		if (1) {
			float* dbuf = (float*) finalImageDepth.mem.mapMemory(0, windowHeight*windowWidth*4, {});
			float* buf = (float*) malloc(windowWidth*windowHeight*4);
			for (int y=0; y<windowHeight; y++)
				for (int x=0; x<windowWidth; x++)
						buf[y*windowWidth+x] = dbuf[y*windowWidth+x];
			finalImageDepth.mem.unmapMemory();

			auto fd_ = open("tstDepth.bmp", O_RDWR | O_TRUNC | O_CREAT, S_IRWXU);
			write(fd_, buf, windowWidth*windowHeight*4);
			close(fd_);
			free(buf);
		}
		*/

		// fmt::print(" - done frame.\n");
	}


};


int main(int argc, char** argv) {

	bool debug = false;
	for (int i=0; i<argc; i++) if (strcmp(argv[i], "dbg") == 0 or strcmp(argv[i], "debug") == 0) debug = true;

	RtApp app;
	app.debug = debug;
	app.windowWidth = 1000;
	app.windowHeight = 800;
	app.windowWidth = app.windowHeight = 512;
	// app.windowWidth = app.windowHeight = 1024;
	app.headless = not debug;

	std::ifstream ifs(argv[1]);
	json jobj = json::parse(ifs);
	auto &entries = jobj["entries"];
	app.jobj = jobj;

	app.initVk();

	app.rtr->setDataLoaderSleepMicros(4'000);

	const double EarthMajorRadius = jobj["earthMajorRadius"];

		int i=0;
		for (auto ent : entries) {
			int j =0;
			std::vector<double> camera8 = ent["camera"];
			std::vector<double> base = ent["base"];
			std::vector<std::vector<double>> perturbedPoses = ent["perturbed"];
			std::vector<std::vector<double>> logDiffs = ent["logDiffs"];

			std::vector<std::vector<double>> poses = {base};
			for (auto& pp: perturbedPoses) poses.push_back(pp);

			double hfov = 2.0f * std::atan(.5f * camera8[0] / camera8[2]);
			double vfov = 2.0f * std::atan(.5f * camera8[1] / camera8[3]);
			double near = camera8[6] / EarthMajorRadius;
			double far  = camera8[7] / EarthMajorRadius;
			CameraSpec cs { camera8[0], camera8[1], hfov, vfov, near, far };

			fmt::print(" - Camera {} {} :: {} {}\n", cs.w, cs.h, cs.fx(), cs.fy());

			for (auto &pose : poses) {
				Eigen::Map<RowMatrix34d> p { pose.data() };
				Vector3d pos = p.topRightCorner<3,1>();
				RowMatrix3d R = p.topLeftCorner<3,3>();
				app.camera->setSpec(cs);
				app.camera->setPosition(pos.data());
				app.camera->setRotMatrix(R.data());
				((SetCamera*)app.camera.get())->recompute_view();

				int k = 0;
				if (app.debug) {
					int okayInRow = 0;
					for (k=0; k<40000; k++) {
						app.render();
						usleep(10'000);
						if (app.advance) {
							app.rtr->allowUpdate = true;
							app.advance = false;
							break;
						}
						if (app.done_updating()) {
							okayInRow++;
						} else okayInRow = 0;
						if (okayInRow == 2) {
							app.rtr->allowUpdate = false;
							fmt::print(" - done {}:{} at {} (took {} loops)\n", i, j, pos.transpose(), k);
							// break;
						}
					}
				} else {

					int okayInRow = 0;
					app.readyToSave = false;
					for (k=0; k<400; k++) {
						app.render();
						usleep(5'000);
						if (app.done_updating()) okayInRow++;
						else okayInRow = 0;

						// The next render is possibly done, so copy. This makes extra copies, but that's ok
						if (okayInRow >= 1)
							app.readyToSave = true;
						else
							app.readyToSave = false;

						if (okayInRow >= 2) {
							// Map headless images.
							// Save files.
							
							uint8_t* dbuf = (uint8_t*) app.finalImage.mem.mapMemory(0, app.windowHeight*app.windowWidth*4, {});
							uint8_t* buf = (uint8_t*) malloc(app.windowWidth*app.windowHeight*3);
							for (int y=0; y<app.windowHeight; y++)
								for (int x=0; x<app.windowWidth; x++)
									for (int c=0; c<3; c++) {
										buf[y*app.windowWidth*3+x*3+c] = dbuf[y*app.windowWidth*4+x*4+c];
									}
							app.finalImage.mem.unmapMemory();
							//int stbi_write_jpg(char const *filename, int w, int h, int comp, const void *data, int quality);
							stbi_write_jpg("tst.jpg", app.windowWidth, app.windowHeight, 3, buf, 90);
							free(buf);

							// Next pose.
							break;
						}
					}
					fmt::print(" - done {}:{} at {} (took {} loops)\n", i, j, pos.transpose(), k);
				}
				j++;
			}
			i++;
	}

	return 0;
}

