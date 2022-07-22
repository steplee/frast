#include "utils.hpp"
#include "frastVk/gt/ftr/ftr.h"

struct RenderSetsMultiConfig {
	// uint32_t N = 3000;
	uint32_t N = 30;
	std::vector<double> chaoses { 0.001f, .1f, .2f };
	// std::vector<double> chaoses { .2f, .2f, .25f, .3f, .35f };
	inline uint32_t M() { return 1 + chaoses.size(); }

	std::string outDir = "/data/gearth/gtSetsMulti1/";
};

static bool DO_STOP = false;
static void sighandler(int sig) { DO_STOP = true; }


class RenderSetsMultiApp : public BaseApp, public HeadlessCopyMixin<RenderSetsMultiApp> {

	public:

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		std::shared_ptr<RtRenderer> rtr;
		std::shared_ptr<FtRenderer> ftr;

		RtTypes::Config rtCfg;
		FtTypes::Config ftCfg;
		RenderSetsMultiConfig rsCfg;
		bool renderWithRt = true;

		uint32_t setIdx=0, viewIdx=0;

		nlohmann::json meta;

		inline RenderSetsMultiApp(const AppConfig& cfg, const RtTypes::Config& rtCfg, const FtTypes::Config& ftCfg, const RenderSetsMultiConfig& rsCfg) : BaseApp(cfg), ftCfg(ftCfg), rtCfg(rtCfg), rsCfg(rsCfg) {
			std::string img_dir = fmt::format("{}images", rsCfg.outDir);
			bool err = mkdirs(img_dir);
			if (err) exit(1);
		}

		inline virtual ~RenderSetsMultiApp() {
			std::string imageInfoPath = fmt::format("{}imageInfo.json", rsCfg.outDir);
			std::ofstream ofs{imageInfoPath, std::ios::out|std::ios::binary};
			ofs << meta;
			fmt::print(" - Wrote '{}'\n", imageInfoPath);
		}

		inline virtual void initVk() {
			BaseApp::initVk();

			CameraSpec spec { (double)windowWidth, (double)windowHeight, 45*M_PI/180. };
			camera = std::make_shared<SetAppCamera>();

			Eigen::Vector3d pos0 { 0.136273, -1.03348, 0.552593 };
			Eigen::Matrix<double,3,3,Eigen::RowMajor> R0;
			R0.row(2) = -pos0.normalized();
			R0.row(0) =  R0.row(2).cross(Eigen::Vector3d::UnitZ()).normalized();
			R0.row(1) =  R0.row(2).cross(R0.row(0)).normalized();
			R0.transposeInPlace();
			// camera->setPosition(pos0.data());
			// camera->setRotMatrix(R0.data());
			RowMatrix4d pose0 { RowMatrix4d::Identity() };
			pose0.topLeftCorner<3,3>() = R0;
			pose0.topRightCorner<3,1>() = pos0;
			dynamic_cast<SetAppCamera*>(camera.get())->set(pose0, spec);

			window->addIoUser(camera.get());
			renderState.camera = camera.get();


			rtr = std::make_shared<RtRenderer>(rtCfg);
			rtr->init(mainDevice, dpool, simpleRenderPass, mainQueue, frameDatas[0].cmd, cfg);
			ftr = std::make_shared<FtRenderer>(ftCfg);
			ftr->init(mainDevice, dpool, simpleRenderPass, mainQueue, frameDatas[0].cmd, cfg);

			initHeadlessCopyMixin();

			buildSets();
		}

		inline virtual void handleCompletedHeadlessRender(RenderState& rs, FrameData& fd) override {
			helper_handleCompletedHeadlessRender(rs, fd, &simpleRenderPass.depthImages[fd.scIndx]);

			Image depthScaled { (int)windowWidth, (int)windowHeight, Image::Format::GRAY };
			Image color { (int)windowWidth, (int)windowHeight, Image::Format::RGB };
			color.alloc();
			depthScaled.alloc();
			float min_depth=1.f, max_depth=0.f;
			{
				Map<Matrix<uint8_t,-1,3,RowMajor>> dst_ { color.buffer, color.w*color.h, 3 };
				Map<Matrix<uint8_t,-1,4,RowMajor>> src_ { (uint8_t*)bufColor.mappedAddr, color.w*color.h, 4 };
				dst_ = src_.leftCols(3);
			}
			{
				Map<Matrix<uint8_t,-1,1>> dst_ { depthScaled.buffer, depthScaled.w*depthScaled.h, 1 };
				Map<Matrix<float,-1,1>> rawDepth_ { (float*)bufDepth.mappedAddr, depthScaled.w*depthScaled.h, 1 };
				for (int i=0; i<depthScaled.w*depthScaled.h; i++) if (rawDepth_[i] != 1.f and rawDepth_[i] != 0.f) min_depth = std::min(rawDepth_[i], min_depth);
				for (int i=0; i<depthScaled.w*depthScaled.h; i++) if (rawDepth_[i] != 1.f and rawDepth_[i] != 0.f) max_depth = std::max(rawDepth_[i], max_depth);
				// min_depth = rawDepth_.minCoeff();
				// max_depth = rawDepth_.maxCoeff();
				rawDepth_.array() -= min_depth;
				rawDepth_.array() /= (max_depth - min_depth);
				dst_ = (rawDepth_ * 255.999).cwiseMax(0).cwiseMin(255.).cast<uint8_t>();
				meta[std::to_string(setIdx) + "_" + std::to_string(viewIdx)] = std::pair<float,float>(min_depth,max_depth);
			}

			EncodedImage ecolor, edepth;
			bool err = encode_jpeg(ecolor, color) or encode_jpeg(edepth, depthScaled);
			assert(not err);

			std::ofstream ofs_color{fmt::format("{}images/{:06d}_{:06d}_c.jpg", rsCfg.outDir, setIdx, viewIdx), std::ios::out|std::ios::binary},
			              ofs_depth{fmt::format("{}images/{:06d}_{:06d}_e.jpg", rsCfg.outDir, setIdx, viewIdx), std::ios::out|std::ios::binary};
			ofs_color.write((char*)ecolor.data(), ecolor.size());
			ofs_depth.write((char*)edepth.data(), edepth.size());
			// fmt::print(" - Wrote set {} view {} -> {} (depths {} {})\n", setIdx,viewIdx, fmt::format("{}images/{:06d}_{:06d}_c.jpg", rsCfg.outDir, setIdx, viewIdx), min_depth,max_depth);
		}

		inline void update() {
			rtr->defaultUpdate(camera.get());
			ftr->defaultUpdate(camera.get());
		}

		virtual void doRender(RenderState& rs) override {
			auto &fd = *rs.frameData;
			auto &cmd = fd.cmd;

			// while (rtr->asksInflight() > 0) { usleep(10'000); }
			assert(rtr->asksInflight() == 0);
			assert(ftr->asksInflight() == 0);

			// RowMatrix4f mvpf;
			// rs.mvpf(mvpf.data());

			simpleRenderPass.begin(fd.cmd, fd);
			if (renderWithRt) rtr->render(rs, fd.cmd);
			else              ftr->render(rs, fd.cmd);
			simpleRenderPass.end(fd.cmd, fd);

			// if (fd.n % 60 == 0)
			if (fd.n % 50 == 0)
				fmt::print(" - Rendering frame {} with {} active tiles\n", fd.n, rtr->activeTiles());
		}


		///////////////////////////////////////////////
		// Set Creation
		///////////////////////////////////////////////

		inline CameraSpec createCameraSpec(int i, int j) {
			constexpr float scale = 40.f;
			constexpr float min   = 25.f;
			float hfov = (((((i+235)*99737) % 9999) / 9999.f) * scale + min) * static_cast<float>(M_PI) / 180.f;
			return CameraSpec { (double)windowWidth, (double)windowHeight, hfov };
		}

		inline BasePose createBasePose(RtTypes::BoundingBox& obb, CameraSpec& spec) {
			// Here's a pretty simple algorithm to start with:
			//          - Compute center of bbox, use it as lookAt target.
			//          - For lookAt eye, use focal-length divided by bbox extent
			//          - For lookAt up, use any normal vector to (target - eye)
			Vector3d ctr = obb.ctr.cast<double>();
			Vector3d nrl = obb.ctr.normalized().cast<double>();
			// double z = (spec.fy() / spec.h) * obb.extents.head<2>().mean();
			// You should multiply 2 since fy/h will give half of image length,
			// then you should multiply outputHeight/256 (which in this case is 2)
			double z = (windowWidth/256.) * 2. * (spec.fy() / spec.h) * obb.extents.maxCoeff();
			// double z = obb.extents.maxCoeff() * 5.;

			Vector3d target = ctr;
			Vector3d eye = ctr + nrl * z;
			// Vector3d up = Vector3d::UnitZ();
			Vector3d up_ = (Vector3d::UnitZ() * .1 + Vector3d::Random()).normalized();

			// world-from-camera rotation
			RowMatrix3d R;
			R.col(2) = (target - eye).normalized();
			R.col(0) = up_.cross(R.col(2)).normalized();
			R.col(1) = -R.col(0).cross(R.col(2)).normalized();

			BasePose out;
			out.pose.topLeftCorner<3,3>() = R;
			out.pose.topRightCorner<3,1>() = eye;
			out.pose.row(3) << 0,0,0,1.;

			out.eye = eye;
			out.tgt = target;
			out.up = R.col(1);

			return out;
		}

		inline Pose createModifiedPose(RtTypes::BoundingBox& obb, CameraSpec& spec, const BasePose& base, double chaos) {
			Vector3d ctr = obb.ctr.cast<double>();
			Vector3d nrl = obb.ctr.normalized().cast<double>();

			Vector3d eye, target, up;

			if (0) {
				//
				// Method 1:
				//      Simply translate eye/tgt and rotate up
				//
				Vector3d up0    = base.up;
				eye    = base.eye.array() + Eigen::Array3d::Random() * (chaos * obb.extents.maxCoeff());
				target = base.tgt.array() + Eigen::Array3d::Random() * (chaos * obb.extents.maxCoeff());
				up     = AngleAxisd(chaos*M_PI*4.5, nrl) * up0;
				up = up0;
			} else if (1) {
				//
				// Method 2:
				//      Rotate eye with origin at tgt. Rotate up. Move tgt small amount
				//

				Vector3d up0    = base.up;
				target    = base.tgt;
				eye    = base.eye;
				up     = AngleAxisd(chaos*M_PI*.5, nrl) * up0;

				Vector3d rpy = Array3d::Random() * chaos * M_PI * Array3d { 1,1,1 };

				RowMatrix3d LTP = getLtp(target);
				RowMatrix3d localR = Quaterniond {
					AngleAxisd(rpy(2), Vector3d::UnitZ()) *
					AngleAxisd(rpy(1), Vector3d::UnitY()) *
					AngleAxisd(rpy(0), Vector3d::UnitX()) }.toRotationMatrix();
				eye = target + LTP * localR * LTP.transpose() * (eye - target);

				target = base.tgt.array() + Eigen::Array3d::Random() * (chaos * obb.extents.maxCoeff());

				// Randomly move camera closer. @t, the alt offset, will be around 1 usually, but can range [.3, 2]
				auto rr = Eigen::Array4d::Random() * .5 + .5;
				auto t = 1. + (rr(3) > .5 ? rr.head<3>().prod() : -rr.head<3>().prod()*.7);
				eye = (eye - target) * t + target;

				if (0) {
					fmt::print(" - createModifiedPose (chaos {}):\n", chaos);
					fmt::print("\t - eye0  {}, altMult {}\n", base.eye.transpose(), t);
					fmt::print("\t - eye   {}\n", eye.transpose());
					fmt::print("\t - tgt0  {}\n", base.tgt.transpose());
					fmt::print("\t - tgt   {}\n", target.transpose());
					fmt::print("\t - up0   {}\n", base.up.transpose());
					fmt::print("\t - rpy:  {}\n", rpy.transpose());
					fmt::print("\t - ltp:\n{}\n", LTP);
					fmt::print("\t - localR:\n{}\n", localR);
				}
			}


			// world-from-camera rotation
			RowMatrix3d R;
			R.col(2) = (target - eye).normalized();
			R.col(0) = up.cross(R.col(2)).normalized();
			R.col(1) = -R.col(0).cross(R.col(2)).normalized();
			// fmt::print(" - R det {}\n", R.determinant());

			Pose out;
			out.topLeftCorner<3,3>() = R;
			out.topRightCorner<3,1>() = eye;
			out.row(3) << 0,0,0,1.;

			return out;
		}

		std::vector<Set> sets;
		inline void buildSets() {

			struct RtCoordAndBbox {
				EIGEN_MAKE_ALIGNED_OPERATOR_NEW
				RtTypes::Coord coord;
				RtTypes::BoundingBox bbox;
			};
			struct FtCoordAndBbox {
				EIGEN_MAKE_ALIGNED_OPERATOR_NEW
				FtTypes::Coord coord;
				FtTypes::BoundingBox bbox;
			};

			//
			// Grab N obbs, evenly distributed. This is done by copying all entries, then randomly drawing N from it
			//

			// Step 1: gather all obbs
			std::vector<RtCoordAndBbox> allRtObbs;
			std::vector<FtCoordAndBbox> allFtObbs;
			for (auto &it : *rtr->obbMap) {
				allRtObbs.push_back({it.first, it.second});
			}
			for (auto &it : *ftr->obbMap) {
				allFtObbs.push_back({it.first, it.second});
			}


			// Step 2: pick random tiles from @ftr that have valid @rtr tiles nearby
			//
			// That would require a O(n^2) matching process
			// So, it is accelerated by a pretty hacky approximate search method.
			// Roughly:
			//		- @rtr & @ftr tiles are mapped to a joint layer index by looking at log2(geoError)
			//		- @rtr & @ftr tiles are further organized to a 3d grid according to the center of the bbox.
			//		         A grid cell has a max occupancy of CELL_SIZE^3 elements.
			//		- Based on the cell that the tile is mapped to, @rtr tiles are stored in an unordered_map
			//		- Finally, the same level + cell mapping is performed on shuffled @ftr tiles.
			//		  Each @ftr tiles searches the list of @ftr tiles corresponding to its level:cell
			//		  If an @ftr tile has any @rtr tiles within a radius of its max(extents), it is considered good.
			//
			//	The pose creation will happen with respect to the @ftr OBB. Because each @ftr tile is verified to have
			//	a similar @rtr tile nearby, the basePose and modifiedPoses will also render well for @rtr.
			//
			//  NOTE XXX TODO:
			//  I did not really test this other than verifying that it appears to work.
			//  There's several possible issues:
			//		- Cell boundaries are too harsh and may true-positive matches are missed
			//		- The geoError quantization is not aligned well (e.g. ftr matches to wrong rtr level)
			//		- Cell coordinates overflow (only 21 bits are used per x/y/z float)
			//		- Cell mapping breaks at low or hi levels
			//

			auto computeLevelFromGeoError = [](float geoError) {
				int lvl = std::log2f(geoError) + 32;
				assert(lvl > 0 and lvl < 32);
				return lvl;
			};
			auto computeCellFromCtr = [](const Vector3f& ctr, int lvl) {
				Vector3f a = (ctr * .499f).array() + .5f;
				// Group into blocks of max size 100^3
				constexpr float CELL_SIZE = 500.f;
				// TODO XXX NOTE: I did not really think if overflow on a deep level is possible here (it probably is?)
				uint64_t c = (static_cast<uint64_t>(a(0) * static_cast<float>(1 << lvl) / CELL_SIZE) << 42)
				           | (static_cast<uint64_t>(a(1) * static_cast<float>(1 << lvl) / CELL_SIZE) << 21)
				           | (static_cast<uint64_t>(a(2) * static_cast<float>(1 << lvl) / CELL_SIZE) <<  0);
				return c;
			};

			fmt::print(" - Organizing {} rt OBBs into hacky search structure...\n", allRtObbs.size());
			std::vector<std::unordered_map<uint64_t,std::vector<RtCoordAndBbox>>> rtObbMapPerLvl(30);
			std::vector<float> rtErrorLevels;
			for (auto& cb : allRtObbs) {
				int lvl = computeLevelFromGeoError(cb.coord.geoError());
				uint64_t cell = computeCellFromCtr(cb.bbox.ctr, lvl);
				rtObbMapPerLvl[lvl][cell].push_back(cb);
			}

			fmt::print(" - Scanning through {} ft OBBs and attempting matching to rt tiles...\n", allFtObbs.size());
			std::vector<FtCoordAndBbox> ftObbs;
			std::shuffle(allFtObbs.begin(), allFtObbs.end(), std::default_random_engine(0));
			int i;
			for (i=0; ftObbs.size() < rsCfg.N and i < allFtObbs.size(); i++) {
				if (DO_STOP) break;
				auto &cb = allFtObbs[i];
				int lvl = computeLevelFromGeoError(cb.coord.geoError());
				uint64_t cell = computeCellFromCtr(cb.bbox.ctr, lvl);

				float maxExtent = cb.bbox.extents.maxCoeff();
				bool good = false;
				int nNearby = 0;


				// Two methods:
				//		1) Check for any 1 neighbor
				//		2) Check four >4 neighbors
				if (1) {
					for (const auto& rtCb : rtObbMapPerLvl[lvl][cell]) {
						float dist = (cb.bbox.ctr - rtCb.bbox.ctr).norm();
						if (dist < maxExtent * 1.1f) {
							fmt::print(" - found {}th good match {} <-> {} (dist {})\n", ftObbs.size(), cb.coord.toString(), rtCb.coord.toString(), dist);
							good = true;
							break;
						}
					}
				}
				else {
					for (const auto& rtCb : rtObbMapPerLvl[lvl][cell]) {
						float dist = (cb.bbox.ctr - rtCb.bbox.ctr).norm();
						if (dist < maxExtent*3.f) {
							nNearby++;
						}
					}
					good = nNearby > 4;
					if (good) fmt::print(" - found {}th good match ({} nearby)\n", ftObbs.size(), nNearby);
				}


				if (good) {
					ftObbs.push_back(cb);
				}
				// else fmt::print(" - candidate bbox {} had no good nearby rtr tiles! Skipping.\n", i);
			}
			fmt::print(fmt::fg(fmt::color::lime_green),
					" - Scanned {} / {} ft tiles to find {} valid / {} asked obbs.\n", i, allFtObbs.size(), ftObbs.size(), rsCfg.N);


			// Sorting will should greatly speed up the rendering. This is for two reasons:
			//    1) Tiles may not need to re-loaded at all if the next view is close to the previous.
			//    2) The disk cache will have the tiles hot.
			std::sort(ftObbs.begin(), ftObbs.end(), [](const auto& a, const auto& b) {
					return a.coord < b.coord;
			});

			sets.resize(rsCfg.N);
			for (int i=0; i<rsCfg.N; i++) {
				auto &coord = ftObbs[i].coord;
				auto &obb   = ftObbs[i].bbox;
				auto &set = sets[i];

				CameraSpec baseSpec { createCameraSpec(i,0) };
				BasePose basePose = createBasePose(obb, baseSpec);
				set.poses.push_back((Pose&)basePose);
				set.camInfos.push_back(baseSpec);

				for (int j=0; j<rsCfg.M()-1; j++) {
					CameraSpec specj { createCameraSpec(i,j) };
					Pose posej = createModifiedPose(obb, specj, basePose, rsCfg.chaoses[j]);

					set.poses.push_back(posej);
					set.camInfos.push_back(specj);
				}
			}
			fmt::print(" - Built {} sets.\n", sets.size());


			// Export to json
			nlohmann::json jobj;
			nlohmann::json jsets = nlohmann::json::array();
			for (int i=0; i<rsCfg.N; i++) {
				nlohmann::json jset;
				jset["views"] = nlohmann::json::array();
				jset["projs"] = nlohmann::json::array();

				for (int j=0; j<rsCfg.M(); j++) {
					std::vector<double> view { sets[i].poses[j].data(), sets[i].poses[j].data()+16 };
					std::vector<double> proj(16);
					sets[i].camInfos[j].compute_projection(proj.data());

					jset["views"].push_back(view);
					jset["projs"].push_back(proj);
				}

				jsets.push_back(jset);
			}
			jobj["sets"] = std::move(jsets);
			std::string outFile = rsCfg.outDir + "sets.json";
			std::ofstream ofs { outFile };
			ofs << jobj;
			fmt::print(" - Wrote '{}'\n", outFile);
		}

		inline SetAppCamera* getCamera() { return dynamic_cast<SetAppCamera*>(camera.get()); }
};


static void run_rt(std::vector<std::string> args) {

	AppConfig appCfg;
	// appCfg.windowSys = AppConfig::WindowSys::eGlfw;
	appCfg.windowSys = AppConfig::WindowSys::eHeadless;
	appCfg.width  = 512;
	appCfg.height = 512;
	appCfg.queueCount = 4;

	/*
	RtTypes::Config rtCfg;
	rtCfg.rootDir = "/data/gearth/many3_wgs/";
	rtCfg.obbIndexPaths = {"/data/gearth/many3_wgs/index.v1.bin"};
	rtCfg.uploadQueueNumber = 1;

	FtTypes::Config ftCfg {
			// .obbIndexPaths = {"/data/naip/tampa/index.v1.bin", "/data/naip/mocoNaip/index.v1.bin"},
			// .colorDsetPaths = {"/data/naip/tampa/tampaAoi.ft", "/data/naip/mocoNaip/out.ft"},
			// .obbIndexPaths = {"/data/naip/tampa/index.v1.bin", "/data/naip/mocoNaip/index.v1.bin", "/data/naip/md/index.v1.bin"},
			// .colorDsetPaths = {"/data/naip/tampa/tampaAoi.ft", "/data/naip/mocoNaip/out.ft", "/data/naip/md/md16.ft"},
			.obbIndexPaths = { "/data/naip/md/index.v1.bin"},
			.colorDsetPaths = { "/data/naip/md/md16.ft"},
			.elevDsetPath  = "/data/elevation/srtm/usa.11.ft"
	};
	*/;

	FtTypes::Config ftCfg;
	RtTypes::Config rtCfg;
	rtCfg.uploadQueueNumber = 1;
	ftCfg.uploadQueueNumber = 2;

	for (int i=0; i<args.size(); i++) {
		if (args[i] == "--rt") {

			std::string c = args[i+1];
			std::string idx = c + "/index.v1.bin";
			rtCfg.rootDir = c;
			rtCfg.obbIndexPaths = {idx};
			fmt::print(fmt::fg(fmt::color::magenta), " - added rt dataset {} with index {}\n", c,idx);
			i++;
		}
		if (args[i] == "--ft") {
			std::string c = args[i+1];
			std::string idx = c.substr(0, c.rfind("/")) + "/index.v1.bin";
			ftCfg.colorDsetPaths.push_back(c);
			ftCfg.obbIndexPaths.push_back(idx);
			i++;
			fmt::print(fmt::fg(fmt::color::magenta), " - added ft dataset {} with index {}\n", c,idx);
		}
	}



	// Put thresholds such that it looks nicer :: not though, this could result to aliasing as mip-mapping is not done
	rtCfg.sseThresholdOpen = .45f;
	rtCfg.sseThresholdClose = .7f;
	ftCfg.sseThresholdOpen = .45f;
	ftCfg.sseThresholdClose = .7f;

	RenderSetsMultiConfig rsCfg;

	RenderSetsMultiApp app(appCfg, rtCfg, ftCfg, rsCfg);
	app.initVk();

	// M should be even so that ft/rt have same number of images
	assert(rsCfg.M() % 2 == 0);


	for (int i=0; i<rsCfg.N; i++) {
		for (int j=0; j<rsCfg.M(); j++) {
			app.setIdx = i;
			app.viewIdx = j;
			app.renderWithRt = ((i + j) % 2) == 0;
			
			SetAppCamera* cam = app.getCamera();
			cam->set(app.sets[i].poses[j], app.sets[i].camInfos[j]);

			// Must update twice, because after updating once, we may still want to change more.
			uint32_t sleeps = 0;
			app.update();
			app.update();
			while (app.rtr->asksInflight() != 0 or app.ftr->asksInflight() != 0) {
				usleep(25'000);
				app.update();
				app.update();
				sleeps++;
				if (DO_STOP) return;
			}
			// fmt::print(" - Setting ViewInv\n{}\n   And Proj\n{}\n   Done loading after {} sleeps\n", Map<const RowMatrix4d>{cam->viewInv()}, Map<const RowMatrix4d>{cam->proj()}, sleeps);
			// fmt::print(" - Done loading after {} sleeps\n", sleeps);
			if (DO_STOP) return;

			app.render();
			usleep(100);
		}
	}

}


int main(int argc, char** argv) {

	srand(0);

	std::vector<std::string> args;
	for (int i=0; i<argc; i++) args.push_back(std::string{argv[i]});

	signal(SIGINT, &sighandler);

	run_rt(args);

	return 0;
}

