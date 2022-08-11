#include "../db.h"
#include "../image.h"
#include "../utils/solve.hpp"
#include "../utils/displayImage.h"

#include <iostream>
#include <iomanip>
#include <cassert>
#include <mutex>

#include <gdal_priv.h>
#include <gdal.h>
#include <ogr_core.h>
#include <ogr_spatialref.h>


#include <Eigen/Core>
#include <Eigen/LU>
#include <chrono>

// Moved to makefile
//#define CONVERT_THREADS 4
static_assert(CONVERT_THREADS <= DatasetWritable::MAX_THREADS);

namespace {

// Only include this file in a terminal binary exe.
std::once_flag flag__;

template <int C> bool is_solid_color_(Image& img) {
	uint8_t ctr[C];
	//for (int c=0; c<C; c++) ctr[c] = img.buffer[img.h/2*img.w*C+img.w/2*C+c];
	for (int c=0; c<C; c++) ctr[c] = img.buffer[0+c];

	for (int y=0; y<img.h; y+=2)
	for (int x=0; x<img.w; x+=2)
	for (int c=0; c<C; c++) {
		uint8_t val = img.buffer[y*img.w*C+x*C+c];
		if (val != ctr[c]) return false;
	}
	return true;
}
bool is_solid_color(Image& img) {
	if (img.channels() == 1) return is_solid_color_<1>(img);
	if (img.channels() == 3) return is_solid_color_<3>(img);
	if (img.channels() == 4) return is_solid_color_<4>(img);
	throw std::runtime_error("invalid # channels");
}

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
//using RowAffine3d = Eigen::Transform<Affine,3,Eigen::RowMajor>;

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

struct GdalDset {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	inline GdalDset() {}
	GdalDset(const std::string& path, Image::Format& outFormat);
	~GdalDset();
	GDALDataset* dset = nullptr;
    OGRCoordinateTransformation* wm2prj  = nullptr;
    OGRCoordinateTransformation* prj2wm  = nullptr;

	RowMatrix23d pix2prj;
	RowMatrix23d prj2pix;
	Vector4d tlbr_uwm;
	Vector4d tlbr_prj;

	int w, h;
	int cv_type;
	GDALDataType gdalType;
	int eleSize;
	int nbands;
	GDALRasterBand* bands[4];
	bool bilinearSampling = true;

	// outFormat = prjFormat, except if outFormat = gray and nbands = 3, in which case prjFormat is RGB
	Image::Format outFormat;
	Image::Format prjFormat;
	Image imgPrj;
	Image imgPrjGray;

	bool bboxProj(const Vector4d& bboxProj, int outw, int outh, Image& out) const;
	bool getTile(Image& out, int z, int y, int x, int tileSize=256);
	//cv::Mat remapBuf;
};

GdalDset::GdalDset(const std::string& path, Image::Format& f) : outFormat(f) {
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

	std::cout << " - Dset pix2prj:\n" << pix2prj << "\n";
	std::cout << " - Dset prj2pix:\n" << prj2pix << "\n";

    nbands    = dset->GetRasterCount() >= 3 ? 3 : 1;
	assert(nbands <= 4);
    auto band = dset->GetRasterBand(0 + 1);
    for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i);
	std::cout << " - nbands: " << nbands << "\n";

    gdalType = dset->GetRasterBand(1)->GetRasterDataType();
    if (not(gdalType == GDT_Byte or gdalType == GDT_Int16 or gdalType == GDT_Float32)) {
        std::cerr << " == ONLY uint8_t/int16_t/float32 dsets supported right now." << std::endl;
        exit(1);
    }
	prjFormat = outFormat;
    if (nbands == 3 and gdalType == GDT_Byte) {
		if (outFormat != Image::Format::GRAY and outFormat != Image::Format::RGB)
			throw std::runtime_error("geotiff is 3-channel int8, output can only be GRAY or RGB");
		prjFormat = Image::Format::RGB;
		eleSize = 1;
	}
	else if (nbands == 1 and gdalType == GDT_Byte) {
		if (outFormat != Image::Format::GRAY)
			throw std::runtime_error("geotiff is 1-channel int8, output can only be GRAY");
		eleSize = 1;
	}
	else if (nbands == 1 and gdalType == GDT_Int16) {
		if (outFormat != Image::Format::TERRAIN_2x8)
			throw std::runtime_error("geotiff is 1-channel int16, output can only be TERRAIN_2x8");
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
    double s = .5 / WebMercatorMapScale;
    tlbr_uwm(0) = (pts_[0] * s + .5);
    tlbr_uwm(1) = (pts_[2] * s + .5);
    tlbr_uwm(2) = (pts_[1] * s + .5);
    tlbr_uwm(3) = (pts_[3] * s + .5);
}

GdalDset::~GdalDset() {
    if (wm2prj) OCTDestroyCoordinateTransformation(wm2prj);
    if (prj2wm) OCTDestroyCoordinateTransformation(prj2wm);
	if (dset) { delete dset; dset = 0; }
}



bool GdalDset::bboxProj(const Vector4d& bboxProj, int outw, int outh, Image& out) const {
	// AtomicTimerMeasurement tg(t_gdal);
    //out.create(outh, outw, cv_type);

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
                                  out.buffer, outw, outh, gdalType,
                                  nbands, nullptr,
                                  eleSize * nbands, eleSize * nbands * outw, eleSize,
                                  &arg);

		// TODO If converting from other terrain then GMTED, must modify here
		if (out.format == Image::Format::TERRAIN_2x8)
			transform_gmted((uint16_t*) out.buffer, outh, outw);

        return err != CE_None;
    } else if (xoff + xsize >= 1 and xoff < w and yoff + ysize >= 1 and yoff < h) {
        // case where there is partial overlap

        // NOTE: TODO I haven't really verified this is correct!
        // TODO: Haven't tasted non-unit aspect ratios

        Eigen::Vector4i inner{std::max(0, xoff),
                              std::max(0, yoff),
                              std::min(w - 1, xoff + xsize),
                              std::min(h - 1, yoff + ysize)};
        float           sx      = ((float)outw) / xsize;
        float           sy      = ((float)outh) / ysize;
        int             inner_w = inner(2) - inner(0), inner_h = inner(3) - inner(1);
        int             read_w = (inner(2) - inner(0)) * sx, read_h = (inner(3) - inner(1)) * sy;
        //printf(" - partial bbox: %dh %dw %dc\n", read_h, read_w, out.channels()); fflush(stdout);
        if (read_w <= 0 or read_h <= 0) return 1;

		Image buf { read_h, read_w, imgPrj.format };
		buf.alloc();
        auto            err = dset->RasterIO(GF_Read,
                                  inner(0), inner(1), inner_w, inner_h,
                                  buf.buffer,
                                  read_w, read_h, gdalType,
                                  nbands, nullptr,
                                  eleSize * nbands, eleSize * nbands * read_w, eleSize * 1, nullptr);
        if (err != CE_None) return true;

		// TODO If converting from other terrain then GMTED, must modify here
		if (out.format == Image::Format::TERRAIN_2x8)
			transform_gmted((uint16_t*) buf.buffer, read_h, read_w);

        float in_pts[8]  = {0, 0, sx * inner_w, 0, 0, sy * inner_h, sx*inner_w, sy*inner_h};
        float out_pts[8] = {sx * (inner(0) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(0) - xoff),
                            sy * (inner(3) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(3) - yoff)};

		float H[9];
		solveHomography(H, in_pts, out_pts);
		buf.warpPerspective(out, H);


        return false;
    } else {
		memset(out.buffer, 0, out.w*out.h*out.channels());
        return true;
    }
}

}
