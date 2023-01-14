#include <unistd.h>

#include "frastgl/core/app.h"
#include "frastgl/core/render_state.h"
#include "ftr.h"

#include <chrono>

using namespace frast;

//
// No need for this to not use the main thread -- except that is an example for future applications.
//

class TestApp : public App {
	public:

		inline TestApp(const AppConfig& cfg)
			: App(cfg)
		{
			thread = std::thread(&TestApp::loop, this);
		}

		inline ~TestApp() {
			assert(isDone and "you should not destroy TestApp until done");
			if (thread.joinable()) thread.join();
		}

		virtual void render(RenderState& rs) override {
			window.beginFrame();

			fmt::print(" - render\n");
			glClearColor(0,0,.01,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			ftr->defaultUpdate(rs.camera);
			ftr->render(rs);


			window.endFrame();
			
		}

		virtual void doInit() override {
			FtTypes::Config cfg;
			cfg.debugMode = true;

			#warning "this is invalid, but functionaly works"
			cfg.obbIndexPaths = {"/data/naip/mocoNaip/index.v1.bin"};
			// cfg.obbIndexPaths = {"/data/naip/mocoNaip/moco.fft.obb"};
			cfg.colorDsetPaths = {"/data/naip/mocoNaip/moco.fft"};

			ftr = std::make_unique<FtRenderer>(cfg);
			ftr->init(this->cfg);
		}

	protected:

		std::unique_ptr<FtRenderer> ftr;

		std::thread thread;

		inline void loop() {
			int frames=0;

			init();

			CameraSpec spec(cfg.w, cfg.h, 45.0 * M_PI/180);
			SphericalEarthMovingCamera cam(spec);

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
				usleep(33'000);
				frames++;

				auto now_time = std::chrono::high_resolution_clock::now();
				float dt = std::chrono::duration_cast<std::chrono::microseconds>(now_time - last_time).count() * 1e-6;
				last_time = now_time;

				cam.step(dt);
				RenderState rs(&cam);
				rs.frameBegin();

				render(rs);

				if (frames>5000) break;
			}

			fmt::print(" - Destroying ftr in render thread\n");
			ftr = nullptr;

			isDone = true;
		}

	public:
		bool isDone = false;
};


int main() {


	AppConfig appCfg;

	TestApp app(appCfg);

	while (!app.isDone) sleep(1);


	return 0;
}
