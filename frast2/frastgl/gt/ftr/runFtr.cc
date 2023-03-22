#include <unistd.h>

#include "frast2/frastgl/core/app.h"
#include "frast2/frastgl/core/imgui/imgui_app.h"
#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/extra/earth/earth.h"
#include "frast2/frastgl/extra/frustum/frustum.h"
#include "frast2/frastgl/extra/textSet/textSet.h"
#include "frast2/frastgl/extra/loaders/obj.h"
#include "ftr.h"

#include <chrono>
#include <signal.h>

using namespace frast;

//
// No need for this to not use the main thread -- except that is an example for future applications.
//

using BaseClass_App = App;
// using BaseClass_App = ImguiApp;

static bool g_stop = false;
void handle_signal(int s) {
	g_stop = true;
}

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
			glCheck("beginFrame");

			if (moveCaster) {
				Eigen::Map<const RowMatrix4d> view { rs.view() };
				Eigen::Map<const RowMatrix4d> proj { rs.proj() };
				RowMatrix4f matrix = (proj*view).cast<float>();
				ftr->cwd.setMatrix1(matrix.data());
			}

			// fmt::print(" - render\n");
			glClearColor(0,0,.01,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glCheck("clear");

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


			glLineWidth(1);
			ftr->defaultUpdate(rs.camera);
			glCheck("ftr update");
			ftr->render(rs);
			glCheck("ftr render");

			earthEllps->render(rs);
			glCheck("earthEllps render");

			if (uav) {
				double time = this->time * .05 * M_PI*2;
				double s = 11 / 6e6;
				double t[3] = {0,0,std::sin(time*1.3)/s};
				double q[4] = {0,0,std::sin(time),std::cos(time)};
				// double s = 1 / 6e6;

				uav->setTransform(t,q,s);

				RowMatrix4d M;
				frustum1->getModelMatrix(M.data());
				RowMatrix4d scaleMatrix(RowMatrix4d::Identity());
				scaleMatrix(3,3) = 1/s;
				M *= scaleMatrix;
				uav->setTransform(M.data());

				uav->renderRecursive(rs);
				glCheck("uav render");
			}


			glLineWidth(2);
			frustum1->render(rs);
			glCheck("frustum1 render");
			textSet->render(rs);
			glCheck("textSet render");

			// if constexpr (std::is_same_v<BaseClass_App, ImguiApp>) ImguiApp::renderUi(rs);

			window.endFrame();
			glCheck("endFrame");
			
		}


		virtual void doInit() override {
			FtTypes::Config cfg;
			cfg.debugMode = true;

			cfg.obbIndexPaths = {"/data/naip/mocoNaip/moco.fft.obb"};
			cfg.colorDsetPaths = {"/data/naip/mocoNaip/moco.fft"};
			// cfg.colorDsetPaths = {"/data/naip/mocoNaip/mocoUtm.fft"};
			// cfg.elevDsetPath = {"/data/elevation/gmted/gmted.fft"};
			cfg.elevDsetPath = {"/data/elevation/srtm/srtm.fft"};

			ftr = std::make_unique<FtRenderer>(cfg);
			ftr->init(this->cfg);
			setExampleCasterData();

			earthEllps = std::make_unique<EarthEllipsoid>();

			textSet = std::make_unique<TextSet>();
			// textSet->setText(0, "HelloWorld");
			float pp[3] = {0.174643 ,-0.770278  ,0.637622};
			float color[4] = {1,1,0,1};
			textSet->setTextPointingNormalToEarth(0, "HelloWorld", pp, .01f, color);

			{
				Eigen::Vector3d pos0 { 0.170643 ,-0.757278  ,0.630622};
				Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
				R0.row(2) = -pos0.normalized();
				R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
				R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
				R0.transposeInPlace();
				frustum1 = std::make_unique<Frustum>();
				RowMatrix4d P;
				for (int i=0; i<20; i++) {
					P.topLeftCorner<3,3>() = R0;
					P.topRightCorner<3,1>() = pos0;
					P.row(3) << 0,0,0,1;
					frustum1->setPose(P.data(), true);
					pos0 += Vector3d{1,0,1} * 100 / 6e6;
				}

				RowMatrix4f casterMatrixFromFrustum;
				frustum1->getCasterMatrix(casterMatrixFromFrustum.data());
				ftr->cwd.setMatrix2(casterMatrixFromFrustum.data());

				// Ellipsoid
				auto ellps = frustum1->getOrCreateEllipsoid();
				float yellow[4] = {1.f, 1.f, .1f, .5f};
				ellps->setColor(yellow);
				RowMatrix4f Pf = P.cast<float>();
				// Pf.topLeftCorner<3,3>() *= 100 / 6e6;
				ellps->setModel(Pf.data());
				auto square = [](float a){return a*a;};
				RowMatrix3f cov = RowMatrix3f::Identity() * square(1000/6e6);
				ellps->setModelFromInvViewAndCov(Pf.data(), cov.data());
			}


			ObjLoader uavLoader{"../data/reaper.obj"};
			uav = std::unique_ptr<Object>(new Object{uavLoader.getRoot()});
			Shader* fixmeAwful = new Shader();
			get_basicMeshNoTex_shader(*fixmeAwful);
			uav->program = fixmeAwful->prog;
			fmt::print(" - uav program is {}\n", uav->program);


		}

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override {
			if (action == GLFW_PRESS and key == GLFW_KEY_M) moveCaster = !moveCaster;
			if (action == GLFW_PRESS and key == GLFW_KEY_K) if (ftr) ftr->flipDebugMode();
			return false;
		}

	protected:

		std::unique_ptr<FtRenderer> ftr;
		std::unique_ptr<EarthEllipsoid> earthEllps;
		std::unique_ptr<Frustum> frustum1;
		std::unique_ptr<TextSet> textSet;
		std::unique_ptr<Object> uav;

		std::thread thread;
		float time=0;

		inline void loop() {
			int frames=0;

			init();

			CameraSpec spec(cfg.w, cfg.h, 45.0 * M_PI/180);
			SphericalEarthMovingCamera cam(spec);

			Eigen::Vector3d pos0 { 0.174643 ,-0.770278  ,0.637622};
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

			while (!g_stop) {
				usleep(33'000);
				frames++;

				auto now_time = std::chrono::high_resolution_clock::now();
				float dt = std::chrono::duration_cast<std::chrono::microseconds>(now_time - last_time).count() * 1e-6;
				time += dt;
				last_time = now_time;

				cam.step(dt);
				RenderState rs(&cam);
				rs.frameBegin();

				render(rs);

				if (frames>5000) break;
			}

			fmt::print(" - Destroying ftr in render thread\n");
			ftr = nullptr;

			ftr = nullptr;
			earthEllps = nullptr;
			frustum1 = nullptr;
			textSet = nullptr;
			uav = nullptr;

			isDone = true;
		}

		bool moveCaster = true;
		void setExampleCasterData() {
			// ftr->setCasterInRenderThread

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
				static_cast<uint8_t*>(tstImg.data)[y*512*4+x*4+3] = 100;
			}
			ftr->cwd.setImage(tstImg);
			// ftr->cwd.setColor1(color1);
			// ftr->cwd.setColor2(color2);
			ftr->cwd.setMask(0b11);
			// ftr->cwd.setMask(0b0);
		}

	public:
		bool isDone = false;
};



int main() {

	signal(SIGINT, &handle_signal);

	AppConfig appCfg;
	// appCfg.w = 1024;
	appCfg.w = 1920;
	appCfg.h = 1080;

	TestApp app(appCfg);

	while (!app.isDone) sleep(1);


	return 0;
}
