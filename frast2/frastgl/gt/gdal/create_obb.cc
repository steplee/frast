#include <fmt/core.h>
#include <sys/stat.h>
#include <Eigen/Core>
#include "gdal.h"

#include <fstream>
#include <unordered_set>

#include "../ftr/conversions.hpp"

namespace {
using namespace frast;

bool file_exists(const std::string& path) {
	struct stat sb;
	int			result = stat(path.c_str(), &sb);
	if (result == 0) return true;
	if (result != 0 and errno == ENOENT) return false;
	throw std::runtime_error(fmt::format("file_exists('{}') failed with error '{}'", path, strerror(errno)));
}

using RowMatrixX3f = Eigen::Matrix<float, -1, 3, Eigen::RowMajor>;

RowMatrixX3f get_ecef_points_of_tile(const BlockCoordinate& coord, GdalDataset& colorDset, GdalDataset& elevDset) {
	Vector4d tlbrWm	 = colorDset.getLocalTileBoundsWm(coord.x(), coord.y(), coord.z());
	// fmt::print("tlbrWm: {} {} {} {}\n", tlbrWm(0), tlbrWm(1), tlbrWm(2), tlbrWm(3));
	Vector4d tlbrUwm = tlbrWm / WebMercatorMapScale;

	int S = 4;

	// fmt::print(" - elev tlbr {} {} {} {}\n", tlbrWm[0], tlbrWm[1], tlbrWm[2], tlbrWm[3]);
	cv::Mat elevBuf(S, S, CV_16UC1);
	elevDset.getWm(tlbrWm, elevBuf);
	// elevBuf.convertTo(elevBuf, CV_32FC1, 1./8.);

	for (uint16_t yy = 0; yy < 8 - 1; yy++)
		for (uint16_t xx = 0; xx < 8 - 1; xx++) {
			uint16_t a = ((yy + 0) * S) + (xx + 0);
			uint16_t b = ((yy + 0) * S) + (xx + 1);
			uint16_t c = ((yy + 1) * S) + (xx + 1);
			uint16_t d = ((yy + 1) * S) + (xx + 0);
		}

	uint16_t* elevData = (uint16_t*)elevBuf.data;

	float lvlScale = 1.f;

	RowMatrixX3f out(S * S, 3);


	for (int32_t yy = 0; yy < S; yy++)
		for (int32_t xx = 0; xx < S; xx++) {
			int32_t ii = ((S - 1 - yy) * S) + xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(S - 1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(S - 1);
			float xxx  = xxx_ * tlbrUwm(0) + (1 - xxx_) * tlbrUwm(2);
			// float yyy = yyy_ * tlbrUwm(3) + (1-yyy_) * tlbrUwm(1);
			float yyy  = yyy_ * tlbrUwm(1) + (1 - yyy_) * tlbrUwm(3);

			float zzz = (elevData[(yy)*elevBuf.cols + xx] / 8.0) / WebMercatorMapScale;

			out.row(ii) << xxx, yyy, zzz;
			// fmt::print(" - vert {} {} {} | {} {}\n", xxx,yyy,zzz, xxx_,yyy_);
		}

	// unit_wm_to_ecef(out.data(), S*S, out.data(), 6);
	unit_wm_to_ecef(out.data(), S * S, out.data(), 3);
	return out;
}

RowMatrixX3f get_ecef_points_of_tile_global(int x, int y, int z, GdalDataset& colorDset, GdalDataset& elevDset) {
	Vector4d tlbrWm	 = colorDset.getGlobalTileBoundsWm(x,y,z);
	Vector4d tlbrUwm = tlbrWm / WebMercatorMapScale;

	int S = 4;

	// fmt::print(" - elev tlbr {} {} {} {}\n", tlbrWm[0], tlbrWm[1], tlbrWm[2], tlbrWm[3]);
	cv::Mat elevBuf(S, S, CV_16UC1);
	elevDset.getWm(tlbrWm, elevBuf);
	// elevBuf.convertTo(elevBuf, CV_32FC1, 1./8.);

	for (uint16_t yy = 0; yy < 8 - 1; yy++)
		for (uint16_t xx = 0; xx < 8 - 1; xx++) {
			uint16_t a = ((yy + 0) * S) + (xx + 0);
			uint16_t b = ((yy + 0) * S) + (xx + 1);
			uint16_t c = ((yy + 1) * S) + (xx + 1);
			uint16_t d = ((yy + 1) * S) + (xx + 0);
		}

	uint16_t* elevData = (uint16_t*)elevBuf.data;

	float lvlScale = 1.f;

	RowMatrixX3f out(S * S, 3);


	for (int32_t yy = 0; yy < S; yy++)
		for (int32_t xx = 0; xx < S; xx++) {
			int32_t ii = ((S - 1 - yy) * S) + xx;
			// int32_t ii = ((yy)*cfg.vertsAlongEdge)+xx;

			float xxx_ = static_cast<float>(xx) / static_cast<float>(S - 1);
			float yyy_ = static_cast<float>(yy) / static_cast<float>(S - 1);
			float xxx  = xxx_ * tlbrUwm(0) + (1 - xxx_) * tlbrUwm(2);
			// float yyy = yyy_ * tlbrUwm(3) + (1-yyy_) * tlbrUwm(1);
			float yyy  = yyy_ * tlbrUwm(1) + (1 - yyy_) * tlbrUwm(3);

			float zzz = (elevData[(yy)*elevBuf.cols + xx] / 8.0) / WebMercatorMapScale;

			out.row(ii) << xxx, yyy, zzz;
			// fmt::print(" - vert {} {} {} | {} {}\n", xxx,yyy,zzz, xxx_,yyy_);
		}

	// unit_wm_to_ecef(out.data(), S*S, out.data(), 6);
	unit_wm_to_ecef(out.data(), S * S, out.data(), 3);
	return out;
}


void create_obb_file(const std::string& obbPath, const std::string& colorPath, const std::string& dtedPath) {
	std::ofstream ofs(obbPath, std::ios_base::binary);
	assert(ofs.good());

	GdalDataset colorDset(colorPath, false);
	GdalDataset elevDset (dtedPath , true);

	// no need for super accurate stuff here.
	elevDset.setUseSubpixelOffsets(false);

	std::unordered_set<uint64_t> base;

	int w = colorDset.w;
	int h = colorDset.h;
	int nseen = 0;

	int maxZ = colorDset.deepestLevelZ;
	int minZ = colorDset.shallowestLevelZ;
	for (int z = minZ; z <= maxZ; z++) {
		int ovr = maxZ - z;
		// int ovr = maxZ - z - 1;

		int levelW = (w + ((1<<ovr)-1)) >> ovr;
		int levelH = (h + ((1<<ovr)-1)) >> ovr;

		int levelNseen = 0;

		/*
		for (int ty=0; ty<(levelH+255)/256+1; ty++) {
			for (int tx=0; tx<(levelW+255)/256+1; tx++) {
				// fmt::print(" - iter z={} ovr={} w={} h={} x={} y={}\n", z, ovr, levelW, levelH, tx, ty);
				if (colorDset.haveLocalTile(tx,ty,ovr)) {
						BlockCoordinate localCoord {ovr,ty,tx};
						BlockCoordinate globalCoord = colorDset.localToGlobalCoord(localCoord.x(), localCoord.y(), localCoord.z());
						BlockCoordinate parent { globalCoord.z() - 1, globalCoord.y() / 2, globalCoord.x() / 2 };
						if (z > minZ and base.find(parent) == base.end()) continue;
						base.insert(globalCoord.c);

						RowMatrixX3f ecefPts = get_ecef_points_of_tile(localCoord, colorDset, elevDset);
						Vector3f ctr = ecefPts.colwise().mean();
						Vector3f ext = (ecefPts.rowwise() - ctr.transpose()).array().abs().matrix().colwise().maxCoeff();
						Vector4f q { 1,0,0,0 };

						static_assert(sizeof(globalCoord) == 8);
						ofs.write((const char*)&globalCoord, sizeof(globalCoord));
						Eigen::Matrix<float,10,1> v; v << ctr, ext, q;
						ofs.write((const char*)v.data(), sizeof(float)*10);

						levelNseen++;
						if (nseen++ % 10'000 == 0 or levelNseen == 1) {
							fmt::print(" - on {:6d}, at {} {} {}, (ctr {} {} {}) (ext*R1 {} {} {})\n",
									// nseen, level, ty, tx,
									nseen, globalCoord.z(), globalCoord.y(), globalCoord.x(),
									ctr(0), ctr(1), ctr(2),
									ext(0)*R1, ext(1)*R1, ext(2)*R1);
						}
				}
			}
		}
		*/

		int d = 1 << (maxZ - z);
		Vector4i lvlTlbr {
			(colorDset.deepestLevelTlbr(0)        ) / d,
			(colorDset.deepestLevelTlbr(1)        ) / d,
			(colorDset.deepestLevelTlbr(2) + d - 1) / d,
			(colorDset.deepestLevelTlbr(3) + d - 1) / d,
		};
		fmt::print("LEVEL: {}, d: {}, tlbr sz: {} {}\n",
				z, d,
				lvlTlbr(2) - lvlTlbr(0),
				lvlTlbr(3) - lvlTlbr(1));

		for (int y=lvlTlbr(1); y<lvlTlbr(3); y++) {
			for (int x=lvlTlbr(0); x<lvlTlbr(2); x++) {
						RowMatrixX3f ecefPts = get_ecef_points_of_tile_global(x,y,z, colorDset, elevDset);
						Vector3f ctr = ecefPts.colwise().mean();
						Vector3f ext = (ecefPts.rowwise() - ctr.transpose()).array().abs().matrix().colwise().maxCoeff();
						Vector4f q { 1,0,0,0 };

						BlockCoordinate globalCoord{z,y,x};
						static_assert(sizeof(globalCoord) == 8);
						ofs.write((const char*)&globalCoord, sizeof(globalCoord));
						Eigen::Matrix<float,10,1> v; v << ctr, ext, q;
						ofs.write((const char*)v.data(), sizeof(float)*10);

						levelNseen++;
						if (nseen++ % 10'000 == 0 or levelNseen == 1) {
							fmt::print(" - on {:6d}, at {} {} {}, (ctr {} {} {}) (ext*R1 {} {} {})\n",
									// nseen, level, ty, tx,
									nseen, x,y,z,
									ctr(0), ctr(1), ctr(2),
									ext(0)*R1, ext(1)*R1, ext(2)*R1);
						}

			}
		}


		if (levelNseen == 0) break;
	}

	fmt::print(" - done writing '{}'\n", obbPath);
	// exit(1);
}
}  // namespace

namespace frast {
bool maybeCreateObbFile(const GdalTypes::Config& cfg) {
	assert(cfg.obbIndexPaths.size() == cfg.colorDsetPaths.size());
	std::string dtedPath = cfg.elevDsetPath;
	for (int i = 0; i < cfg.obbIndexPaths.size(); i++) {
		std::string obbPath	  = cfg.obbIndexPaths[i];
		std::string colorPath = cfg.colorDsetPaths[i];
		assert(file_exists(colorPath));
		assert(file_exists(dtedPath));
		if (!file_exists(obbPath)) {
			fmt::print("Creating obb file '{}'\n", obbPath);
			create_obb_file(obbPath, colorPath, dtedPath);
		} else {
			fmt::print("Obb file '{}' already exists\n", obbPath);
		}
	}
	return false;
}
}  // namespace frast
