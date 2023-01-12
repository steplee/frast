#pragma once

#include "coordinates.h"
#include "detail/solve.hpp"

#include <algorithm>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include <iostream>
#include <iomanip>
#include <cassert>
#include <mutex>

#include <gdal_priv.h>
#include <gdal.h>
#include <gdalwarper.h>
#include <ogr_core.h>
#include <ogr_spatialref.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <chrono>

namespace {
	// Convert the int16_t -> uint16_t.
	// Change nodata value from -32768 to 0. Clamp min to 0.
	// Clamp max to 8191, then multiply by 8.
	// The final mult is done to get 8 ticks/meter value-precision, so that warps have higher accuracy.
	// That means that the user should convert to float, then divide by 8 after accessing the dataset.
	void transform_gmted(uint16_t* buf, int h, int w) {
		for (int y=0; y<h; y++)
		for (int x=0; x<w; x++) {
			int16_t gmted_val = ((int16_t*)buf)[y*w+x];

			if (gmted_val < 0) gmted_val = 0;
			if (gmted_val > 8191) gmted_val = 8191;

			uint16_t val = gmted_val * 8;

			buf[y*w+x] = val;
		}
	}
}

enum class ImageFormat {
	GRAY =0,
	RGB =1,
	RGBA =2,
	TERRAIN_2x8 =3
};

using namespace Eigen;
template <class T, int r, int c>
using RowMatrix = Eigen::Matrix<T,r,c,Eigen::RowMajor>;
using RowMatrix4d = Eigen::Matrix<double,4,4,Eigen::RowMajor>;
using RowMatrix4d = Eigen::Matrix<double,4,4,Eigen::RowMajor>;
using RowMatrix3d = Eigen::Matrix<double,3,3,Eigen::RowMajor>;
using RowMatrix23d = Eigen::Matrix<double,2,3,Eigen::RowMajor>;
using RowMatrix2Xd = Eigen::Matrix<double,2,-1,Eigen::RowMajor>;
using RowMatrixX2d = Eigen::Matrix<double,-1,2,Eigen::RowMajor>;
using RowMatrixX2f = Eigen::Matrix<float,-1,2,Eigen::RowMajor>;
using RowMatrix2Xf = Eigen::Matrix<float,2,-1,Eigen::RowMajor>;

static std::once_flag flag__;

namespace frast {
namespace {

	struct MyGdalDataset {

		inline MyGdalDataset() {}
		MyGdalDataset(const std::string& path);
		~MyGdalDataset();
		GDALDataset* dset = nullptr;
		OGRCoordinateTransformation* wm2prj  = nullptr;
		OGRCoordinateTransformation* prj2wm  = nullptr;

		RowMatrix23d pix2prj;
		RowMatrix23d prj2pix;
		Vector4d tlbr_uwm;
		Vector4d tlbr_prj;

		int w, h;
		// int cv_type;
		GDALDataType gdalType;
		bool isTerrain = false;
		int eleSize;
		int nbands;
		GDALRasterBand* bands[4];
		bool bilinearSampling = true;
		// bool clampToBorder = true; // For tiles on the edge, where to make missing pixels black or the clamped nearest color
		bool clampToBorder = false;

		GDALWarpOptions *warpOptions = nullptr;

		cv::Mat getWmTile(const double wmTlbr[4], int w, int h, int c);
		// Vector4d bboxProj(const Vector4d& bboxProj, cv::Mat& out);
		Vector4d bboxPix(const Vector4d& bboxPix, cv::Mat& out);

		void getTlbrForLevel(uint64_t tlbr[4], int lvl);
	};

void MyGdalDataset::getTlbrForLevel(uint64_t tlbr[4], int lvl) {
	tlbr[0] = static_cast<uint64_t>((tlbr_uwm[0]+1)*.5 * (1<<lvl));
	tlbr[1] = static_cast<uint64_t>((tlbr_uwm[1]+1)*.5 * (1<<lvl));
	tlbr[2] = static_cast<uint64_t>((tlbr_uwm[2]+1)*.5 * (1<<lvl));
	tlbr[3] = static_cast<uint64_t>((tlbr_uwm[3]+1)*.5 * (1<<lvl));
}

cv::Mat MyGdalDataset::getWmTile(const double wmTlbr[4], int w, int h, int c) {
	auto cvType = c == 1 ? CV_8UC1 : c == 3 ? CV_8UC3 : c == 4 ? CV_8UC4 : -1;
	auto internalCvType = nbands == 1 ? CV_8UC1 : nbands == 3 ? CV_8UC3 : nbands == 4 ? CV_8UC4 : -1;

	assert(cvType != -1);
	assert(internalCvType != -1);
	cv::Mat out(w,h,internalCvType);

	double cornersWm[8] = {
		wmTlbr[0], wmTlbr[1],
		wmTlbr[2], wmTlbr[1],
		wmTlbr[2], wmTlbr[3],
		wmTlbr[0], wmTlbr[3] };

	double corners_[8] = {
		wmTlbr[0], wmTlbr[2], wmTlbr[2], wmTlbr[0],
		wmTlbr[1], wmTlbr[1], wmTlbr[3], wmTlbr[3] };
    wm2prj->Transform(4, corners_, corners_+4, nullptr);
	Map<Matrix<double,4,2>> corners { corners_ };
	corners = (corners * prj2pix.topLeftCorner<2,2>().transpose()).eval().array().rowwise() + prj2pix.col(2).array().transpose();

	AlignedBox2d aabb { corners.row(0).transpose() };
	aabb.extend(corners.row(1).transpose());
	aabb.extend(corners.row(2).transpose());
	aabb.extend(corners.row(3).transpose());
	// fmt::print(" - pix aabb: {} {}\n", aabb.min().transpose(), aabb.max().transpose());

	Vector2d tl_pix = aabb.min();
	Vector2d br_pix = aabb.max();
	Matrix<double,4,2> corners_nat; corners_nat <<
		tl_pix(0), tl_pix(1),
		br_pix(0), tl_pix(1),
		br_pix(0), br_pix(1),
		tl_pix(0), br_pix(1);
	corners_nat = (corners_nat * pix2prj.topLeftCorner<2,2>().transpose()).eval().array().rowwise() + pix2prj.col(2).array().transpose();
    prj2wm->Transform(4, corners_nat.data(), corners_nat.data()+4, nullptr);
	// fmt::print(" - corners nat: {}\n", corners_nat);




	Vector4d tlbr_pix { aabb.min()(0), aabb.min()(1), aabb.max()(0), aabb.max()(1) };
	cv::Mat sampledImg;
	sampledImg.create(h,w,internalCvType);
	Vector4d tlbr_pix_sampled = bboxPix(tlbr_pix, sampledImg);

	// fmt::print(" - sampled tlbr: {}\n", tlbr_pix_sampled.transpose());
	// cv::imshow("sampled", sampledImg);



	float H[9];
	float in_pts[] = {
		0,0,
		(float)(w-1),0,
		(float)(w-1),(float)(h-1),
		0,(float)(h-1)
	};
	float out_pts[] = {
		(float)(w * (corners_nat(0,0)-wmTlbr[0]) / (wmTlbr[2]-wmTlbr[0])), (float)(h * (corners_nat(0,1)-wmTlbr[1]) / (wmTlbr[3]-wmTlbr[1])),
		(float)(w * (corners_nat(1,0)-wmTlbr[0]) / (wmTlbr[2]-wmTlbr[0])), (float)(h * (corners_nat(1,1)-wmTlbr[1]) / (wmTlbr[3]-wmTlbr[1])),
		(float)(w * (corners_nat(2,0)-wmTlbr[0]) / (wmTlbr[2]-wmTlbr[0])), (float)(h * (corners_nat(2,1)-wmTlbr[1]) / (wmTlbr[3]-wmTlbr[1])),
		(float)(w * (corners_nat(3,0)-wmTlbr[0]) / (wmTlbr[2]-wmTlbr[0])), (float)(h * (corners_nat(3,1)-wmTlbr[1]) / (wmTlbr[3]-wmTlbr[1]))
	};
	solveHomography(H, in_pts, out_pts);

	cv::Mat HH(3,3,CV_32F,H);
	cv::warpPerspective(sampledImg, out, HH, cv::Size{out.cols, out.rows});
	// fmt::print(" - final H:\n{}\n", HH);

	// cv::imshow("finalTile", out);
	// cv::waitKey(1);

	if (internalCvType == 3 and cvType == 1) {
		cv::cvtColor(out,out, cv::COLOR_RGB2GRAY);
	} else if (internalCvType == 4 and cvType == 1) {
		cv::cvtColor(out,out, cv::COLOR_RGBA2GRAY);
	} else if (cvType == 1 and internalCvType == 3) {
		cv::cvtColor(out,out, cv::COLOR_GRAY2BGR);
	} else if (cvType == 1 and internalCvType == 4) {
		cv::cvtColor(out,out, cv::COLOR_GRAY2BGRA);
	}

	return out;
}




Vector4d MyGdalDataset::bboxPix(const Vector4d& bboxPix, cv::Mat& out) {
	// AtomicTimerMeasurement tg(t_gdal);
    //out.create(outh, outw, cv_type);
	int outh = out.rows, outw = out.cols;

	auto internalCvType = nbands == 1 ? CV_8UC1 : nbands == 3 ? CV_8UC3 : nbands == 4 ? CV_8UC4 : -1;
	assert(internalCvType != -1);

	int out_c = out.channels();
	if (nbands >= 3 and out_c == 1)
		out.create(outh, outw, CV_8UC3);


    Vector2d tl = bboxPix.head<2>();
    Vector2d br = bboxPix.tail<2>();
    if (tl(0) > br(0)) std::swap(tl(0), br(0));
    if (tl(1) > br(1)) std::swap(tl(1), br(1));
    int xoff  = tl(0);
    int yoff  = tl(1);
    int xsize = (int)(.5 + br(0) - tl(0));
    int ysize = (int)(.5 + br(1) - tl(1));
    if (xsize == 0) xsize++;
    if (ysize == 0) ysize++;

    if (xoff > 0 and xoff + xsize < w and yoff > 0 and yoff + ysize < h) {
        // auto arg = nullptr;
        GDALRasterIOExtraArg arg;
        arg.nVersion = RASTERIO_EXTRA_ARG_CURRENT_VERSION;
        if (bilinearSampling)
            arg.eResampleAlg = GRIORA_Bilinear;
        else
            arg.eResampleAlg = GRIORA_NearestNeighbour;
        arg.pfnProgress                  = 0;
        arg.pProgressData                = 0;
        arg.bFloatingPointWindowValidity = 0;
        auto err                         = dset->RasterIO(GF_Read,
                                  xoff, yoff, xsize, ysize,
                                  out.data, outw, outh, gdalType,
                                  nbands, nullptr,
                                  eleSize * nbands, eleSize * nbands * outw, eleSize,
                                  &arg);

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain)
			transform_gmted((uint16_t*) out.data, outh, outw);

        if (err != CE_None)
			return Vector4d::Zero();

    } else if (xoff + xsize >= 1 and xoff < w and yoff + ysize >= 1 and yoff < h) {
        // case where there is partial overlap
        // WARNING: This is not correct. Border tiles have artifacts at corners.
        Eigen::Vector4i inner{std::max(0, xoff),
                              std::max(0, yoff),
                              std::min(w - 1, xoff + xsize),
                              std::min(h - 1, yoff + ysize)};
        float           sx      = ((float)outw) / xsize;
        float           sy      = ((float)outh) / ysize;
        int             inner_w = inner(2) - inner(0), inner_h = inner(3) - inner(1);
        int             read_w = (inner(2) - inner(0)) * sx, read_h = (inner(3) - inner(1)) * sy;
        //printf(" - partial bbox: %dh %dw %dc\n", read_h, read_w, out.channels()); fflush(stdout);
        if (read_w <= 0 or read_h <= 0) return Vector4d::Zero();

		cv::Mat tmp;
		tmp.create(read_h, read_w, internalCvType);

        auto            err = dset->RasterIO(GF_Read,
                                  inner(0), inner(1), inner_w, inner_h,
								  tmp.data,
                                  read_w, read_h, gdalType,
                                  nbands, nullptr,
                                  eleSize * nbands, eleSize * nbands * read_w, eleSize * 1, nullptr);
        if (err != CE_None) return Vector4d::Zero();

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain)
			transform_gmted((uint16_t*) tmp.data, outh, outw);

        float in_pts[8]  = {0, 0, sx * inner_w, 0, 0, sy * inner_h, sx*inner_w, sy*inner_h};
        float out_pts[8] = {sx * (inner(0) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(0) - xoff),
                            sy * (inner(3) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(3) - yoff)};

		// xoff = inner(0); yoff = inner(1);
		// xsize = inner_w; ysize = inner_h;

		float H[9];
		solveHomography(H, in_pts, out_pts);

		// buf.warpPerspective(out, H, clampToBorder);
		cv::Mat HH(3,3,CV_32F,H);
		cv::warpPerspective(tmp, out, HH, cv::Size{out.cols, out.rows});

    } else {
		memset(out.data, 0, out.rows*out.cols*out.channels());
        return Vector4d::Zero();
    }

	Vector2d tl_sampled = Vector2d { xoff, yoff };
	Vector2d br_sampled = Vector2d { xoff+xsize, yoff+ysize };
	// Vector2d br_sampled = pix2prj * Vector3d { xoff+xsize-1, yoff+ysize-1, 1 };
	if (tl_sampled(0) > br_sampled(0)) std::swap(tl_sampled(0), br_sampled(0));
	if (tl_sampled(1) > br_sampled(1)) std::swap(tl_sampled(1), br_sampled(1));
	return Vector4d { tl_sampled(0), tl_sampled(1), br_sampled(0), br_sampled(1) };
}


/*
Vector4d MyGdalDataset::bboxProj(const Vector4d& bboxProj, cv::Mat& out) {
	// AtomicTimerMeasurement tg(t_gdal);
    //out.create(outh, outw, cv_type);
	int outh = out.rows, outw = out.cols;

	auto internalCvType = nbands == 1 ? CV_8UC1 : nbands == 3 ? CV_8UC3 : nbands == 4 ? CV_8UC4 : -1;

	int out_c = out.channels();
	if (nbands >= 3 and out_c == 1)
		out.create(outh, outw, CV_8UC3);


    Vector2d tl = (prj2pix * Vector3d{bboxProj(0), bboxProj(1), 1.});
    Vector2d br = (prj2pix * Vector3d{bboxProj(2) + bboxProj(0), bboxProj(3) + bboxProj(1), 1.});
    if (tl(0) > br(0)) std::swap(tl(0), br(0));
    if (tl(1) > br(1)) std::swap(tl(1), br(1));
    int xoff  = tl(0);
    int yoff  = tl(1);
    int xsize = (int)(.5 + br(0) - tl(0));
    int ysize = (int)(.5 + br(1) - tl(1));
    if (xsize == 0) xsize++;
    if (ysize == 0) ysize++;

    if (xoff > 0 and xoff + xsize < w and yoff > 0 and yoff + ysize < h) {
        // auto arg = nullptr;
        GDALRasterIOExtraArg arg;
        arg.nVersion = RASTERIO_EXTRA_ARG_CURRENT_VERSION;
        if (bilinearSampling)
            arg.eResampleAlg = GRIORA_Bilinear;
        else
            arg.eResampleAlg = GRIORA_NearestNeighbour;
        arg.pfnProgress                  = 0;
        arg.pProgressData                = 0;
        arg.bFloatingPointWindowValidity = 0;
        auto err                         = dset->RasterIO(GF_Read,
                                  xoff, yoff, xsize, ysize,
                                  out.data, outw, outh, gdalType,
                                  nbands, nullptr,
                                  eleSize * nbands, eleSize * nbands * outw, eleSize,
                                  &arg);

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain)
			transform_gmted((uint16_t*) out.data, outh, outw);

        if (err != CE_None)
			return Vector4d::Zero();

    } else if (xoff + xsize >= 1 and xoff < w and yoff + ysize >= 1 and yoff < h) {
        // case where there is partial overlap

        // WARNING: This is not correct. Border tiles have artifacts at corners.

        Eigen::Vector4i inner{std::max(0, xoff),
                              std::max(0, yoff),
                              std::min(w - 1, xoff + xsize),
                              std::min(h - 1, yoff + ysize)};
        float           sx      = ((float)outw) / xsize;
        float           sy      = ((float)outh) / ysize;
        int             inner_w = inner(2) - inner(0), inner_h = inner(3) - inner(1);
        int             read_w = (inner(2) - inner(0)) * sx, read_h = (inner(3) - inner(1)) * sy;
        //printf(" - partial bbox: %dh %dw %dc\n", read_h, read_w, out.channels()); fflush(stdout);
        if (read_w <= 0 or read_h <= 0) return Vector4d::Zero();

		cv::Mat tmp;
		tmp.create(read_h, read_w, internalCvType);

        auto            err = dset->RasterIO(GF_Read,
                                  inner(0), inner(1), inner_w, inner_h,
								  tmp.data,
                                  read_w, read_h, gdalType,
                                  nbands, nullptr,
                                  eleSize * nbands, eleSize * nbands * read_w, eleSize * 1, nullptr);
        if (err != CE_None) return Vector4d::Zero();

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain)
			transform_gmted((uint16_t*) tmp.data, outh, outw);

        float in_pts[8]  = {0, 0, sx * inner_w, 0, 0, sy * inner_h, sx*inner_w, sy*inner_h};
        float out_pts[8] = {sx * (inner(0) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(0) - xoff),
                            sy * (inner(3) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(3) - yoff)};

		// xoff = inner(0); yoff = inner(1);
		// xsize = inner_w; ysize = inner_h;

		float H[9];
		solveHomography(H, in_pts, out_pts);

		// buf.warpPerspective(out, H, clampToBorder);
		cv::Mat HH(3,3,CV_32F,H);
		cv::warpPerspective(tmp, out, HH, cv::Size{out.cols, out.rows});

    } else {
		memset(out.data, 0, out.rows*out.cols*out.channels());
        return Vector4d::Zero();
    }

	Vector2d tl_sampled = pix2prj * Vector3d { xoff, yoff, 1 };
	Vector2d br_sampled = pix2prj * Vector3d { xoff+xsize, yoff+ysize, 1 };
	// Vector2d br_sampled = pix2prj * Vector3d { xoff+xsize-1, yoff+ysize-1, 1 };
	if (tl_sampled(0) > br_sampled(0)) std::swap(tl_sampled(0), br_sampled(0));
	if (tl_sampled(1) > br_sampled(1)) std::swap(tl_sampled(1), br_sampled(1));
	return Vector4d { tl_sampled(0), tl_sampled(1), br_sampled(0), br_sampled(1) };
}
*/

MyGdalDataset::MyGdalDataset(const std::string& path) {
	std::call_once(flag__, &GDALAllRegister);
	dset = (GDALDataset*) GDALOpen(path.c_str(), GA_ReadOnly);


    double g[6];
    dset->GetGeoTransform(g);
    RowMatrix3d pix2prj_;
    pix2prj_ << g[1], g[2], g[0], g[4], g[5], g[3], 0, 0, 1;
    RowMatrix3d prj2pix_ = pix2prj_.inverse();
    pix2prj             = pix2prj_.topRows<2>();
    prj2pix             = prj2pix_.topRows<2>();
    w                   = dset->GetRasterXSize();
    h                   = dset->GetRasterYSize();

	// std::cout << " - Dset pix2prj:\n" << pix2prj << "\n";
	// std::cout << " - Dset prj2pix:\n" << prj2pix << "\n";

    nbands    = dset->GetRasterCount() >= 3 ? 3 : 1;
	assert(nbands <= 4);
    auto band = dset->GetRasterBand(0 + 1);
    for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i);
	// std::cout << " - nbands: " << nbands << "\n";

    gdalType = dset->GetRasterBand(1)->GetRasterDataType();
    if (not(gdalType == GDT_Byte or gdalType == GDT_Int16 or gdalType == GDT_Float32)) {
        std::cerr << " == ONLY uint8_t/int16_t/float32 dsets supported right now." << std::endl;
        exit(1);
    }
    if (nbands == 3 and gdalType == GDT_Byte) {
		eleSize = 1;
	}
	else if (nbands == 1 and gdalType == GDT_Byte) {
		eleSize = 1;
	}
	else if (nbands == 1 and gdalType == GDT_Int16) {
		eleSize = 2;
	}
	else if (nbands == 1 and gdalType == GDT_Float32) {
		throw std::runtime_error("geotiff is float32 input is not supported yet. I have yet to implement terrain conversion from f32 -> TERRAIN_2x8");
		eleSize = 4;
	}
	else assert(false);

    OGRSpatialReference sr_prj, sr_3857, sr_4326;
    char*               pp = const_cast<char*>(dset->GetProjectionRef());
    sr_prj.importFromWkt(&pp);
    sr_3857.importFromEPSG(3857);
    wm2prj  = OGRCreateCoordinateTransformation(&sr_3857, &sr_prj);
    prj2wm  = OGRCreateCoordinateTransformation(&sr_prj, &sr_3857);

    Vector2d tl_prj = pix2prj * Vector3d{0, 0, 1};
    Vector2d br_prj = pix2prj * Vector3d{(double)w, (double)h, 1};
    double   ptsPrj[4]   = { tl_prj(0), tl_prj(1), br_prj(0), br_prj(1) };
    double   pts_[4]   = {tl_prj(0), br_prj(0), tl_prj(1), br_prj(1)};
    prj2wm->Transform(2, pts_, pts_ + 2, nullptr);
    if (pts_[0] > pts_[1]) std::swap(pts_[0], pts_[1]);
    if (pts_[2] > pts_[3]) std::swap(pts_[2], pts_[3]);
    if (ptsPrj[0] > ptsPrj[2]) std::swap(ptsPrj[0], ptsPrj[2]);
    if (ptsPrj[1] > ptsPrj[3]) std::swap(ptsPrj[1], ptsPrj[3]);
	for (int i=0; i<4; i++) tlbr_prj(i) = ptsPrj[i];

    double s = 1. / WebMercatorMapScale;
    tlbr_uwm(0) = (pts_[0] * s);
    tlbr_uwm(1) = (pts_[2] * s);
    tlbr_uwm(2) = (pts_[1] * s);
    tlbr_uwm(3) = (pts_[3] * s);
}

MyGdalDataset::~MyGdalDataset() {
    if (wm2prj) OCTDestroyCoordinateTransformation(wm2prj);
    if (prj2wm) OCTDestroyCoordinateTransformation(prj2wm);
	if (dset) { delete dset; dset = 0; }
	if (warpOptions) {
		GDALDestroyGenImgProjTransformer( warpOptions->pTransformerArg );
		GDALDestroyWarpOptions( warpOptions );
	}
}

}
}
