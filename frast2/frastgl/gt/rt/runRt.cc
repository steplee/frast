#include <unistd.h>

#include "frast2/frastgl/core/app.h"
#include "frast2/frastgl/core/imgui/imgui_app.h"
#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/extra/earth/earth.h"
#include "frast2/frastgl/extra/frustum/frustum.h"
#include "frast2/frastgl/extra/textSet/textSet.h"
#include "rt.h"

#include <chrono>

using namespace frast;

//
// No need for this to not use the main thread -- except that is an example for future applications.
//

using BaseClass_App = App;
// using BaseClass_App = ImguiApp;

class TestApp : public BaseClass_App {
	public:

		inline TestApp(const AppConfig& cfg)
			: BaseClass_App(cfg)
		{
			thread = std::thread(&TestApp::loop, this);
		}

		inline ~TestApp() {
			assert(isDone and "you should not destroy TestApp until done");
			if (thread.joinable()) thread.join();
		}

		virtual void render(RenderState& rs) override {
			window.beginFrame();

			if (moveCaster) {
				Eigen::Map<const RowMatrix4d> view { rs.view() };
				Eigen::Map<const RowMatrix4d> proj { rs.proj() };
				RowMatrix4f matrix = (proj*view).cast<float>();
				rtr->cwd.setMatrix1(matrix.data());
			}

			// fmt::print(" - render\n");
			glClearColor(0,0,.01,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


			glLineWidth(1);
			rtr->defaultUpdate(rs.camera);
			rtr->render(rs);

			earthEllps->render(rs);


			glLineWidth(2);
			frustum1->render(rs);
			textSet->render(rs);

			// if constexpr (std::is_same_v<BaseClass_App, ImguiApp>) ImguiApp::renderUi(rs);

			window.endFrame();
			
		}


		virtual void doInit() override {
			RtTypes::Config cfg;
			cfg.debugMode = true;

			cfg.rootDir = "/data/gearth/naipAoisWgs";
			cfg.obbIndexPaths = {"/data/gearth/naipAoisWgs/index.v1.bin"};
			// cfg.obbIndexPaths = {"/data/gearth/naipAoisWgs/index.v1.bin"};

			rtr = std::make_unique<RtRenderer>(cfg);
			rtr->init(this->cfg);
			// setExampleCasterData();

			earthEllps = std::make_unique<EarthEllipsoid>();

			textSet = std::make_unique<TextSet>();
			// textSet->setText(0, "HelloWorld");
			float pp[3] = {0.172643 ,-0.767278  ,0.650622};
			textSet->setTextPointingNormalToEarth(0, "HelloWorld", pp);

			{
				Eigen::Vector3d pos0 { 0.170643 ,-0.757278  ,0.630622};
				Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
				R0.row(2) = -pos0.normalized();
				R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
				R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
				R0.transposeInPlace();
				frustum1 = std::make_unique<Frustum>();
				RowMatrix4d P;
				P.topLeftCorner<3,3>() = R0;
				P.topRightCorner<3,1>() = pos0;
				P.row(3) << 0,0,0,1;
				frustum1->setPose(P.data());

				// RowMatrix4f casterMatrixFromFrustum;
				// frustum1->getCasterMatrix(casterMatrixFromFrustum.data());
				// rtr->cwd.setMatrix2(casterMatrixFromFrustum.data());
			}




		}

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override {
			if (action == GLFW_PRESS and key == GLFW_KEY_M) moveCaster = !moveCaster;
			if (action == GLFW_PRESS and key == GLFW_KEY_K) if (rtr) rtr->flipDebugMode();
			return false;
		}

	protected:

		std::unique_ptr<RtRenderer> rtr;
		std::unique_ptr<EarthEllipsoid> earthEllps;
		std::unique_ptr<Frustum> frustum1;
		std::unique_ptr<TextSet> textSet;

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

			fmt::print(" - Destroying rtr in render thread\n");
			rtr = nullptr;

			isDone = true;
		}

		bool moveCaster = false;
		void setExampleCasterData() {
			// rtr->setCasterInRenderThread

			// float color1[4] = {.1f,0.f,.1f,.6f};
			// float color2[4] = {.0f,.5f,.2f,.6f};

			// Image tstImg { 512,512,Image::Format::RGBA };
			cv::Mat tstImg(512,512,CV_8UC4);
			for (int y=0; y<512; y++)
			for (int x=0; x<512; x++) {
				uint8_t c = (y % 16 <= 4 and x % 16 <= 4) ? 200 : 0;
				static_cast<uint8_t*>(tstImg.data)[y*512*4+x*4+0] =
				static_cast<uint8_t*>(tstImg.data)[y*512*4+x*4+1] =
				static_cast<uint8_t*>(tstImg.data)[y*512*4+x*4+2] = c;
				static_cast<uint8_t*>(tstImg.data)[y*512*4+x*4+3] = 200;
			}
			rtr->cwd.setImage(tstImg);
			// rtr->cwd.setColor1(color1);
			// rtr->cwd.setColor2(color2);
			rtr->cwd.setMask(0b11);
			// rtr->cwd.setMask(0b0);
		}

	public:
		bool isDone = false;
};


int main() {


	AppConfig appCfg;
	// appCfg.w = 1024;
	appCfg.w = 1920;
	appCfg.h = 1080;

	TestApp app(appCfg);

	while (!app.isDone) sleep(1);


	return 0;
}

