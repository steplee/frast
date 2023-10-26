#include "frast2/flat/reader.h"

/*
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;
*/

#include "frast2/frastgl/core/app.h"
#include "frast2/frastgl/core/imgui/imgui_app.h"
#include "frast2/frastgl/core/render_state.h"
#include "frast2/frastgl/core/cameras.h"
#include "frast2/frastgl/extra/earth/earth.h"
// #include "frast2/frastgl/extra/frustum/frustum.h"
#include "frast2/frastgl/gt/rt/rt.h"
#include "frast2/frastgl/gt/ftr/ftr.h"

#include <condition_variable>

namespace frast {

	struct GtConfig {
		FtTypes::Config ft_cfg;
		RtTypes::Config rt_cfg;
		bool flipY = true; // Flip projection y axis. WARNING: all black?
		// bool flipY = false; // Flip projection y axis.
	};

	inline GtConfig create_gt_app_config(
			std::vector<std::string> ftColorPaths,
			std::string ftDtedPath,
			std::string rtRootPath) {

		GtConfig out;

		if (ftColorPaths.size()) {
			out.ft_cfg.colorDsetPaths = ftColorPaths;
			for (const auto &s : ftColorPaths)
				out.ft_cfg.obbIndexPaths.push_back(s + ".obb");
			out.ft_cfg.elevDsetPath = ftDtedPath;
		}

		if (rtRootPath.length() > 0) {
			out.rt_cfg.debugMode = false;
			out.rt_cfg.rootDir = rtRootPath;
			out.rt_cfg.obbIndexPaths = {rtRootPath + "/obb.obb"};
		}

		return out;
	}


	struct __attribute__((packed)) SetCameraAction {
		static constexpr int ID = 0;
		inline SetCameraAction() {}
		SetCameraAction(const float *invView, const CameraSpec& spec);

		int id = ID;
		float invView[16];
		CameraSpec spec;
	};

	struct __attribute__((packed)) RenderAction {
		static constexpr int ID = 1;
		int id = ID;
		bool want_depth = false;
	};

	struct __attribute__((packed)) StopAction {
		static constexpr int ID = 2;
		int id = ID;
	};

	union Action {
		SetCameraAction setCamera;
		RenderAction render;
		StopAction stop;
		inline int id() const { return setCamera.id; } // should work.
	};

	struct Image {
		int w=0, h=0, c=0;
		bool isFloat = false;
		std::vector<uint8_t> data;
	};

	struct RenderResult {
		int seq;
		Image color;
		Image depth;
	};

	class GtWrapperApp : public App {
		public:
			
			GtWrapperApp(const AppConfig& cfg, const GtConfig& gtCfg);
			~GtWrapperApp();

			virtual void render(RenderState& rs) override;
			virtual void doInit() override;
			virtual bool handleKey(int key, int scancode, int action, int mods) override;

			std::condition_variable requestCv;
			std::condition_variable replyCv;

			void external_setCamera(const SetCameraAction& setc);
			std::unique_ptr<RenderResult> external_askAndWaitForRender(const RenderAction& ra);

		private:

			std::mutex mtx;
			std::deque<Action> actions;
			std::deque<RenderResult> results;

			GtConfig gtCfg;

			void loop();

			std::unique_ptr<RtRenderer> rtr;
			std::unique_ptr<FtRenderer> ftr;
			std::unique_ptr<EarthEllipsoid> earthEllps;

			std::thread thread;

			bool isDone = false;
			bool usingFtr = true;

	};


}

