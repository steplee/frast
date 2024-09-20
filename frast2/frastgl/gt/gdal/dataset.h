#pragma once

#include <gdal_priv.h>
#include <gdal.h>
#include <gdalwarper.h>
#include <ogr_core.h>
#include <ogr_spatialref.h>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/LU>

#include <opencv2/core.hpp>
#include "frast2/coordinates.h"

namespace frast {

using RowMatrix23d = Eigen::Matrix<double,2,3,Eigen::RowMajor>;
using RowMatrix42d = Eigen::Matrix<double,4,2,Eigen::RowMajor>;
using RowMatrix3d = Eigen::Matrix<double,3,3,Eigen::RowMajor>;
using Eigen::Vector4d;
using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::Vector4i;

struct GdalDataset {
	
		GDALDataset* dset = nullptr;

		int w, h;
		GDALDataType gdalType;
		bool isTerrain = false;
		int eleSize;
		int nbands;
		int internalCvType;
		GDALRasterBand* bands[4];
		bool bilinearSampling = true;

		GdalDataset(const std::string& path, bool isTerrain=false);
		~GdalDataset();

		cv::Mat getLocalTile(int x, int y, int overview);
		cv::Mat getGlobalTile(int x, int y, int z);
		Vector4d getWm(const Vector4d& tlbrWm, cv::Mat& out);
		Vector4d getPix(const Vector4d& tlbrPix, cv::Mat& out);

		Vector4d getLocalTileBoundsWm(int x, int y, int overview);
		Vector4d getGlobalTileBoundsWm(int x, int y, int z);
		int getNumOverviews() const;

		RowMatrix23d pix_from_native;
		RowMatrix23d native_from_pix;

		// WARNING: Now defaults to on.
		// bool useSubpixelOffsets = false;
		bool useSubpixelOffsets = true;

		inline void setUseSubpixelOffsets(bool on) { useSubpixelOffsets = on; }

		bool haveLocalTile(int x, int y, int overview);

		// Matching the dataset to the (aligned) WM quadtree.
		int deepestLevelZ;
		int shallowestLevelZ;
		Vector4i deepestLevelTlbr; // integral, pixels.
		Vector4i shallowestLevelTlbr; // integral, pixels.
		BlockCoordinate localToGlobalCoord(int x, int y, int ovr);

};
}
