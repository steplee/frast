#include "gt_app_wrapper.h"

#include <string>
#include <sys/stat.h>

namespace {
bool fileExists(const std::string& f) {
	struct stat s;
	if(stat(f.c_str(),&s) == 0 ) return true;
	return false;
}
}

namespace frast {

	SetCameraAction::SetCameraAction(const float *invView_, const CameraSpec& spec)
		: spec(spec)
	{
		for (int i=0; i<16; i++) invView[i] = invView_[i];
	}

	GtWrapperApp::GtWrapperApp(const AppConfig& cfg, const GtConfig& gtCfg)
		: App(cfg), gtCfg(gtCfg)
	{
		thread = std::thread(&GtWrapperApp::loop, this);
	}

	GtWrapperApp::~GtWrapperApp() {

		{
			std::unique_lock<std::mutex> lck(mtx);
			StopAction stop;
			Action action { .stop = stop };
			actions.push_front(action);
			requestCv.notify_one();
		}

		int trials = 0;
		while (!isDone and trials < 1000) {
			usleep(100);
		}

		assert(isDone and "you should not destroy GtWrapperApp until done");
		fmt::print(" - ~GtWrapperApp joining thread.\n");
		if (thread.joinable()) thread.join();
	}

	void GtWrapperApp::render(RenderState& rs) {
			window.beginFrame();

			glClearColor(0,0,.01,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


			glLineWidth(1);
			if (usingFtr) {
				ftr->defaultUpdate(rs.camera);
				usleep(10);
				ftr->defaultUpdate(rs.camera);
				ftr->render(rs);
			} else if (usingGdal) {
				gdalr->defaultUpdate(rs.camera);
				usleep(10);
				gdalr->defaultUpdate(rs.camera);
				gdalr->render(rs);
			} else {
				rtr->defaultUpdate(rs.camera);
				usleep(10);
				rtr->defaultUpdate(rs.camera);
				rtr->render(rs);
			}

			earthEllps->render(rs);

			window.endFrame();
	}

	void GtWrapperApp::doInit() {
		const auto& ft_cfg = gtCfg.ft_cfg;
		const auto& gdal_cfg = gtCfg.gdal_cfg;
		const auto& rt_cfg = gtCfg.rt_cfg;

		bool askedRt = rt_cfg.rootDir.length() > 0;

		if (ft_cfg.colorDsetPaths.size()) {
			if (fileExists(ft_cfg.colorDsetPaths[0])) {
				ftr = std::make_unique<FtRenderer>(ft_cfg);
				ftr->init(cfg);
			} else {
				throw std::runtime_error(fmt::format("Asked to use FtRenderer root dir '{}', but it does not exist!", ft_cfg.colorDsetPaths[0]));
			}
		} else if (gdal_cfg.colorDsetPaths.size()) {
			maybeCreateObbFile(gdal_cfg);
			if (fileExists(gdal_cfg.colorDsetPaths[0])) {
				gdalr = std::make_unique<GdalRenderer>(gdal_cfg);
				gdalr->init(cfg);
			} else {
				throw std::runtime_error(fmt::format("Asked to use GdalRenderer root dir '{}', but it does not exist!", gdal_cfg.colorDsetPaths[0]));
			}
		}

		if (askedRt) {
			if (fileExists(rt_cfg.rootDir)) {
				rtr = std::make_unique<RtRenderer>(rt_cfg);
				rtr->init(cfg);
			} else {
				throw std::runtime_error(fmt::format("Asked to use RtRenderer root dir '{}', but it does not exist!", rt_cfg.rootDir));
			}
		}

		if (ftr == nullptr and rtr == nullptr and gdalr == nullptr) {
			// logCritical("Viewer needs one-of or both-of ftr and rtr");
			throw std::runtime_error("Viewer needs one-of or both-of ftr and rtr and gdalr");
		}

		usingFtr = (ftr != nullptr); // default to ftr, if it is on.
		usingGdal = gdalr != nullptr;
		if (askedRt and rtr) usingFtr = false;

		earthEllps = std::make_unique<EarthEllipsoid>();
	}

	bool GtWrapperApp::handleKey(int key, int scancode, int action, int mods) {
		return false;
	}

	void GtWrapperApp::external_setCamera(const SetCameraAction& cam) {
		std::unique_lock<std::mutex> lck(mtx);
		Action action { .setCamera = cam };
		actions.push_front(action);
		requestCv.notify_one();
	}
	std::unique_ptr<RenderResult> GtWrapperApp::external_askAndWaitForRender(const RenderAction& ra) {
		if (isDone) return nullptr;

		{
			std::unique_lock<std::mutex> lck(mtx);
			Action action { .render = ra };
			actions.push_front(action);
			requestCv.notify_one();
		}

		usleep(10);

		{
			std::unique_lock<std::mutex> lck(mtx);
			replyCv.wait(lck, [this]() { return results.size() or isDone; });
			if (isDone) {
				return nullptr;
			}

			// We expect _exactly_ one result.
			assert(results.size() == 1);

			auto rr = std::move(results.back());
			results.pop_back();
			return std::make_unique<RenderResult>(std::move(rr));
		}

	}

	void GtWrapperApp::loop() {
			int frames=0;

			init();

			CameraSpec spec(cfg.w, cfg.h, 45.0 * M_PI/180);
			// SphericalEarthMovingCamera cam(spec);
			GlobeCamera cam(spec);
			cam.flipY_ = gtCfg.flipY;

			Eigen::Vector3d pos0 { 0.136273, -1.03348, 0.552593 };
			Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
			R0.row(2) = -pos0.normalized();
			R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
			R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
			R0.transposeInPlace();
			cam.setPosition(pos0.data());
			cam.setRotMatrix(R0.data());

			window.addIoUser(&cam);

			// FIXME: call glViewport...

			auto last_time = std::chrono::high_resolution_clock::now();

			while (true) {
				// usleep(33'000);
				std::unique_lock<std::mutex> lck(mtx);
				requestCv.wait(lck, [this]() { return actions.size(); });

				Action action { std::move(actions.back()) };
				actions.pop_back();
				lck.unlock();

				if (action.id() == SetCameraAction::ID) {
					fmt::print(" - recv set camera action\n");
					SetCameraAction& setCam = action.setCamera;
					double t[3] = {
						(double) setCam.invView[0*4+3],
						(double) setCam.invView[1*4+3],
						(double) setCam.invView[2*4+3],
					};
					double R[9] = {
						(double) setCam.invView[0*4+0],
						(double) setCam.invView[0*4+1],
						(double) setCam.invView[0*4+2],
						(double) setCam.invView[1*4+0],
						(double) setCam.invView[1*4+1],
						(double) setCam.invView[1*4+2],
						(double) setCam.invView[2*4+0],
						(double) setCam.invView[2*4+1],
						(double) setCam.invView[2*4+2] };

					{
						cam.setRotMatrix(R);
						cam.setPosition(t);
						cam.setSpec(setCam.spec);
					}
				}

				if (action.id() == RenderAction::ID) {
					fmt::print(" - recv render action\n");
					RenderAction& renderAction = action.render;


					auto now_time = std::chrono::high_resolution_clock::now();
					float dt = std::chrono::duration_cast<std::chrono::microseconds>(now_time - last_time).count() * 1e-6;
					last_time = now_time;

					cam.step(dt);
					RenderState rs(&cam);
					rs.frameBegin();

					render(rs);

					assert(renderAction.want_depth == false && "not supported yet");
					Image img;
					img.w = App::cfg.w;
					img.h = App::cfg.h;
					img.c = 3;
					img.data.resize(img.w*img.h*img.c);
					glReadPixels(0,0,img.w,img.h,GL_RGB,GL_UNSIGNED_BYTE,img.data.data());
					RenderResult result;
					result.seq = frames;
					result.color = std::move(img);

					{
						std::unique_lock<std::mutex> lck2(mtx);
						results.emplace_back(std::move(result));
						replyCv.notify_one();
					}

					frames++;
				}

				if (action.id() == StopAction::ID) {
					fmt::print(" - recv stop action\n");
					break;
				}

			}

			fmt::print(" - Destroying rtr in render thread\n");
			rtr = nullptr;
			ftr = nullptr;
			gdalr = nullptr;
			earthEllps = nullptr;

			std::unique_lock<std::mutex> lck(mtx);
			isDone = true;
			replyCv.notify_one();
	}

}
