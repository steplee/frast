#include "db.h"
#include "image.h"

#include <iostream>
#include <iomanip>
#include <cassert>
#include <mutex>

#include <gdal_priv.h>
#include <gdal.h>
#include <ogr_core.h>
#include <ogr_spatialref.h>

//#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
//#include <opencv2/imgcodecs.hpp>
//#include <opencv2/highgui.hpp>

#include <Eigen/Core>
#include <Eigen/LU>
//#include <Eigen/Geometry>
#include <chrono>


// Moved to makefile
//#define CONVERT_THREADS 4
static_assert(CONVERT_THREADS <= DatasetWritable::MAX_THREADS);

template <int C> static bool is_solid_color_(Image& img) {
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
static bool is_solid_color(Image& img) {
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
static void transform_gmted(uint16_t* buf, int h, int w) {
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

static std::once_flag flag__;
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
	AtomicTimerMeasurement tg(t_gdal);
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
		auto cv_type = imgPrj.format == Image::Format::TERRAIN_2x8 ? CV_16UC1 : imgPrj.channels() == 3 ? CV_8UC3 : CV_8U;
        cv::Mat         buf(read_h, read_w, cv_type);
        auto            err = dset->RasterIO(GF_Read,
                                  inner(0),
                                  inner(1),
                                  inner_w,
                                  inner_h,
                                  buf.data,
                                  read_w,
                                  read_h,
                                  gdalType,
                                  nbands,
                                  nullptr,
                                  eleSize * nbands,
                                  eleSize * nbands * read_w,
                                  eleSize * 1,
                                  nullptr);
        if (err != CE_None) return true;

		// TODO If converting from other terrain then GMTED, must modify here
		if (out.format == Image::Format::TERRAIN_2x8)
			transform_gmted((uint16_t*) buf.data, read_h, read_w);

		// Nevermind: This won't work if input tiffs are split exactly on edges.
		// This will work if tiffs have some overlap, but that is rare
		/*
		// Make edges of sampled buffer black (nodata).
		// This helps frastMerge avoid weighting bilinear sampled nodata/realData
		// edges, which causes black seams!
		if (yoff < 0)
		buf(cv::Rect{0,0,buf.cols,5})            = cv::Scalar{0};
		if (xoff < 0)
		buf(cv::Rect{0,0,5,buf.rows})            = cv::Scalar{0};
		if (xoff+xsize > w)
		buf(cv::Rect{buf.cols-5,0,5,buf.rows}) = cv::Scalar{0};
		if (yoff+ysize > h)
		buf(cv::Rect{0,buf.rows-5,buf.cols,5}) = cv::Scalar{0};
		*/

        float in_pts[6]  = {0, 0, sx * inner_w, 0, 0, sy * inner_h};
        float out_pts[6] = {sx * (inner(0) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(2) - xoff),
                            sy * (inner(1) - yoff),
                            sx * (inner(0) - xoff),
                            sy * (inner(3) - yoff)};

        cv::Mat in_pts_(3, 2, CV_32F, in_pts);
        cv::Mat out_pts_(3, 2, CV_32F, out_pts);
        cv::Mat A = cv::getAffineTransform(in_pts_, out_pts_);
		if (out.format == Image::Format::GRAY and imgPrj.format == Image::Format::RGB)
			cv::cvtColor(buf,buf,cv::COLOR_RGB2GRAY);
		cv::Mat out_(outh, outw, cv_type, out.buffer);
        cv::warpAffine(buf, out_, A, cv::Size{outw, outh});
        return false;
    } else {
		memset(out.buffer, 0, out.w*out.h*out.channels());
        return true;
    }
}

// Note: pts is taken to be COLUMN MAJOR, that is, like 'xxxyyy'
template <int N>
static void computeTlbr(Vector4d& out, const double *pts) {
	out = Vector4d { 9e19, 9e19, -9e19, -9e19 };
	for (int i=0; i<N; i++) {
		if (pts[i  ] < out(0)) out(0) = pts[i  ]; // x
		if (pts[i  ] > out(2)) out(2) = pts[i  ]; // x
		if (pts[i+N] < out(1)) out(1) = pts[i+N]; // y
		if (pts[i+N] > out(3)) out(3) = pts[i+N]; // y
	}
}

// To get a tile from an input tiff:
//			- Inverse-warp tile corners to prj coords.
//			- Read the AABB of those coords to a buffer
//			- Inverse-warp as many control points as desired from wm -> prj
//			- Bilinear interpolate those mapped points
//			- Bilinear interpolate the control points per pixel and read buffer.
//			     - Since already using opencv, I can:
//						+ Use cv::resize with bilinear interp to get the pixel locations,
//						+ Use cv::remap to do the final bilinear inteprolation.
//
// Note: Take care to ensure that there is a decent overview for the queried box.
//
//

bool GdalDset::getTile(Image& out, int z, int y, int x, int tileSize) {
	int ow = out.w, oh = out.h;

	constexpr bool debug = false;

	//constexpr int rtN = 128;
	constexpr int rtN = 8;
	constexpr int N = rtN * rtN;
	assert(tileSize % rtN == 0);

	bool y_flipped = pix2prj(1,1) < 0;
	double sampleScale = 2; // I'd say 2 looks best. It helps reduce interp effects. 1 is a little blurry.
	int sw = out.w*sampleScale+.1, sh = out.h*sampleScale+.1;

	// Start off as wm, but transformed to prj
	RowMatrix2Xd pts { 2 , N };
	{
		double start = 0, end = 1.;
		//start = .5 / rtN;
		//end   = 1. - .5 / rtN;
		//start = 1. / rtN;
		//end   = 1. - 1. / rtN;
		//start = -1. / rtN;
		//end = 1. + 1. / rtN;
		double s = static_cast<double>(1 << z);
		double scale = WebMercatorMapScale / (s * .5);
		double off_x = (static_cast<double>(x)) * WebMercatorMapScale / s * 2 - WebMercatorMapScale;
		double off_y = (static_cast<double>(y)) * WebMercatorMapScale / s * 2 - WebMercatorMapScale;
		
		//using ArrayT = Array<double,rtN,1>;
		using ArrayT = Array<double,-1,1>;
		auto X = ArrayT::LinSpaced(rtN, start, end) * scale + off_x;
		auto Y = y_flipped ? ArrayT::LinSpaced(rtN, end, start) * scale + off_y
		                   : ArrayT::LinSpaced(rtN, start, end) * scale + off_y;
		//std::cout << " - XY " << ArrayXd::LinSpaced(rtN, start, end).transpose() << "\n";
		for (int y=0; y<rtN; y++)
		for (int x=0; x<rtN; x++)
			pts.col(y*rtN+x) << X(x), Y(y);
	}

	//printf(" - wm  corners: %.3lf %.3lf %.3lf %.3lf\n", pts(0,0), pts(1,0), pts(0,N-1), pts(1,N-1));

    wm2prj->Transform(N, pts.data(), pts.data() + N, nullptr);


	// With high probability, the corners of the input are the corners of the output. Avoid the linear search.
	Vector4d prjTlbr { 9e19, 9e19, -9e19, -9e19 };
	computeTlbr<N>(prjTlbr, pts.data());
	Vector4d prjBbox { prjTlbr(0), prjTlbr(1), prjTlbr(2)-prjTlbr(0), prjTlbr(3)-prjTlbr(1) };

	if (debug) printf(" - prj     corners: %.3lf %.3lf %.3lf %.3lf (%lfw %lfh)\n",
			prjTlbr(0), prjTlbr(1), prjTlbr(2), prjTlbr(3),
			prjTlbr(2)-prjTlbr(0), prjTlbr(3)-prjTlbr(1));
	if (debug) printf(" - dsetPrj corners: %.3lf %.3lf %.3lf %.3lf (%lfw %lfh)\n",
			tlbr_prj(0), tlbr_prj(1), tlbr_prj(2), tlbr_prj(3),
			tlbr_prj(2)-tlbr_prj(0), tlbr_prj(3)-tlbr_prj(1));

	if (imgPrj.w < sw or imgPrj.h < sh) {
		imgPrj = std::move(Image{ sh, sw, prjFormat });
		imgPrj.alloc();
	}

	Image* srcImg = &imgPrj;
	bool res = bboxProj(prjBbox, sw, sh, imgPrj);
	if (outFormat == Image::Format::GRAY and imgPrj.channels() != 1) {
		if (imgPrjGray.w < sw or imgPrjGray.h < sh) {
			imgPrjGray = std::move(Image{ sh, sw, outFormat });
			imgPrjGray.alloc();
		}
		imgPrj.makeGray(imgPrjGray);
		srcImg = &imgPrjGray;
	}
	if (res) {
		//printf(" - Failed to get tile %d %d %d\n", z, y, x); fflush(stdout);
		return res;
	}
	if (is_solid_color(*srcImg)) {
		//printf("Tile %d %d was solid color, not using.\n", y,x);
		return true;
	}

	// TODO: If imgPrj is less channels than the output type, do it here. Otherwise do it after warping.

	//RowMatrix<float,N,2> meshPtsf { N , 2 };
	RowMatrix<float,-1,2> meshPtsf { N , 2 };
	{
		Vector2d off   { prjTlbr(0), prjTlbr(1) };
		Array2f scale { sw/(prjTlbr(2)-prjTlbr(0)), sh/(prjTlbr(3)-prjTlbr(1)) };
		if (y_flipped) {
			//for (int i=0; i<N; i++) meshPtsf.row(i) = (Vector2d{pts(0,i),pts(1,N-1-i)} - off).cast<float>().array() * scale;
			meshPtsf.col(0) = pts.row(0).transpose().cast<float>();
			meshPtsf.col(1) = pts.row(1).reverse().transpose().cast<float>();
			meshPtsf = (meshPtsf.rowwise() - off.transpose().cast<float>()).array().rowwise() * scale.transpose();
		} else
			//for (int i=0; i<N; i++) meshPtsf.row(i) = (pts.col(i) - off).cast<float>().array() * scale;
			meshPtsf = (pts.transpose().rowwise() - off.transpose()).cast<float>().array().rowwise() * scale.transpose();
		//std::cout << " - Mesh Pts:\n" << meshPtsf <<"\n";
	}
	//if (y_flipped) for (int i=0; i<N/2; i++) std::swap(meshPtsf(i,1), meshPtsf(N-i-1,1));

#if 0
	{
		AtomicTimerMeasurement tg(t_warp);
		cv::Mat meshMat { rtN, rtN, CV_32FC2, meshPtsf.data() };
		//meshMat = meshMat(cv::Rect(1,1,rtN-2,rtN-2));
		//std::cout << meshMat << "\n";
		if (meshMat.cols != ow or meshMat.rows != oh) {
			cv::resize(meshMat, meshMat, cv::Size{ow, oh}, 0,0, cv::INTER_LINEAR);
			//cv::resize(meshMat, meshMat, cv::Size{ow, oh}, 0,0, cv::INTER_CUBIC);
		}
		//std::cout << meshMat << "\n";

		//out.create(imgPrj.rows, imgPrj.cols, imgPrj.type());
		cv::remap(imgPrj, out, meshMat, cv::noArray(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
		//cv::remap(imgPrj, out, meshMat, cv::noArray(), cv::INTER_AREA, cv::BORDER_REPLICATE);
	}
#else
	//Image dst { out.rows, out.cols, channels, out.data };
	Image& src = *srcImg;
	Image& dst = out;
	{
		AtomicTimerMeasurement tg(t_warp);
		src.remapRemap(dst, meshPtsf.data(), rtN, rtN);
	}
#endif

	return false;
}








/*
static void test1() {
	GdalDset dset { "/data/naip/mocoNaip/whole.tif" };

	cv::Mat tile ( 256, 256, dset.cv_type );
	//dset.getTile(tile, 20, 648704, 299310);
	//dset.getTile(tile, 20-4, 648704/16, 299310/16);
	dset.getTile(tile, 20-3, 648536/8-1, 299283/8+1);

	cv::cvtColor(tile,tile,cv::COLOR_RGB2BGR);
	cv::imwrite("tile0.jpg", tile);

	cv::Mat grid ( 256 * 3, 256 * 3, dset.cv_type );
	for (int y=0; y<3; y++)
	for (int x=0; x<3; x++) {
		//dset.getTile(tile, 20-3, 648536/8-1+y, 299283/8+x);
		dset.getTile(tile, 20-5, 648536/32-1+y, 299283/32+x);
		tile.copyTo(grid(cv::Rect{x*256,(2-y)*256,256,256}));
	}
	cv::cvtColor(grid,grid,cv::COLOR_RGB2BGR);
	cv::imwrite("grid0.jpg", grid);
}

static void test2() {
	GdalDset* dset[CONVERT_THREADS];
	cv::Mat tile[CONVERT_THREADS];

	for (int i=0; i<CONVERT_THREADS; i++) {
		dset[i] = new GdalDset { "/data/naip/mocoNaip/whole.tif" };
		std::cout << " - dset ptr : " << dset[i]->dset << "\n";
		tile[i] = cv::Mat ( 256, 256, dset[i]->cv_type );
	}

	std::cout << " - Making level 16.\n";

	constexpr int N = 10;
	#pragma omp parallel for schedule(static,4) num_threads(CONVERT_THREADS)
	for (int y=0; y<N; y++)
	for (int x=0; x<N; x++) {
		int tid = omp_get_thread_num();
		dset[tid]->getTile(tile[tid], 20-4, 648536/16-15+y, 299283/16-15+x);
	}

	for (int i=0; i<CONVERT_THREADS; i++) delete dset[i];
}
*/

static void findWmTlbrOfDataset(uint64_t tlbr[4], GdalDset* dset, int lvl) {
	double s = (1lu<<((int64_t)lvl));

	tlbr[0] = (uint64_t)(dset->tlbr_uwm[0] * s) + 4lu;
	tlbr[1] = (uint64_t)(dset->tlbr_uwm[1] * s) + 4lu;
	tlbr[2] = (uint64_t)(dset->tlbr_uwm[2] * s) - 4lu;
	tlbr[3] = (uint64_t)(dset->tlbr_uwm[3] * s) - 4lu;

	tlbr[0] = (uint64_t)(dset->tlbr_uwm[0] * s - 1) + 0lu;
	tlbr[1] = (uint64_t)(dset->tlbr_uwm[1] * s - 1) + 0lu;
	tlbr[2] = (uint64_t)(dset->tlbr_uwm[2] * s     ) - 0lu;
	tlbr[3] = (uint64_t)(dset->tlbr_uwm[3] * s     ) - 0lu;

}

static int test3(const std::string& srcTiff, const std::string& outPath, std::vector<int>& lvls, Image::Format& outFormat, double overrideTlbr[4]) {
	// Open one DB, and one tiff+buffer per thread

	GdalDset* dset[CONVERT_THREADS];
	std::vector<uint8_t> tmpBuf[CONVERT_THREADS];
	//TileImage tileImages[CONVERT_THREADS];
	Image tileImages[CONVERT_THREADS];
	//cv::Mat tile[CONVERT_THREADS];

	int32_t tileSize = 256;

	for (int i=0; i<CONVERT_THREADS; i++) {
		dset[i] = new GdalDset { srcTiff , outFormat };

		std::cout << " - dset ptr : " << dset[i]->dset << "\n";
		//tileImages[i] = TileImage { 256, 256, dset[i]->nbands };
		//tileImages[i].isOverview = false;
		tileImages[i] = Image { 256, 256, outFormat };
		tileImages[i].alloc();
		//tile[i] = cv::Mat ( 256, 256, dset[i]->cv_type, tileImages[i].buffer );
	}

	DatabaseOptions opts;
	//opts.mapSize = 1 << 20;
	DatasetWritable outDset { outPath , opts };

	// Configure #channels and tileSize.
	outDset.setFormat((uint32_t)outFormat);
	outDset.setTileSize(tileSize);


	outDset.configure(CONVERT_THREADS, WRITER_NBUF);
	std::cout << " - beginning" << std::endl;

	// For each level
	//    Get bbox of dataset
	//    For each tile in bbox [multithreaded]
	//        getTile()
	//        putTile()

	for (int lvli = 0; lvli<lvls.size(); lvli++) {
		int lvl = lvls[lvli];

		outDset.sendCommand(DbCommand{DbCommand::BeginLvl,lvl});

		uint64_t tileTlbr[4];
		findWmTlbrOfDataset(tileTlbr, dset[0], lvl);
		if (overrideTlbr[0] != 0 and overrideTlbr[2] != 0) {
			if (overrideTlbr[0] > overrideTlbr[2]) std::swap(overrideTlbr[0], overrideTlbr[2]);
			if (overrideTlbr[1] > overrideTlbr[3]) std::swap(overrideTlbr[1], overrideTlbr[3]);
			tileTlbr[0] = (uint64_t) (((.5 * overrideTlbr[0] / WebMercatorMapScale) + .5) * (1lu << lvl));
			tileTlbr[1] = (uint64_t) (((.5 * overrideTlbr[1] / WebMercatorMapScale) + .5) * (1lu << lvl));
			tileTlbr[2] = (uint64_t) (((.5 * overrideTlbr[2] / WebMercatorMapScale) + .5) * (1lu << lvl) + 1);
			tileTlbr[3] = (uint64_t) (((.5 * overrideTlbr[3] / WebMercatorMapScale) + .5) * (1lu << lvl) + 1);
		}

		int nrows = (tileTlbr[3]-tileTlbr[1]);
		int ncols = (tileTlbr[2]-tileTlbr[0]);
		int totalTiles = nrows*ncols;
		printf(" - Lvl %d:\n",lvl);
		printf(" -        %lu %lu -> %lu %lu\n", tileTlbr[0], tileTlbr[1], tileTlbr[2], tileTlbr[3]);
		printf(" -        %lu rows, %lu cols\n", nrows, ncols);

		//#pragma omp parallel for schedule(static,4) num_threads(CONVERT_THREADS)
		#pragma omp parallel for schedule(dynamic,2) num_threads(CONVERT_THREADS)
		for (uint64_t y=tileTlbr[1]; y<=tileTlbr[3]; y++) {
			int tilesInRow = 0;
			auto startTime = std::chrono::high_resolution_clock::now();
			for (uint64_t x=tileTlbr[0]; x<=tileTlbr[2]; x++) {

				int tid = omp_get_thread_num();
				BlockCoordinate coord { (uint64_t)lvl, y, x};
				//printf(" - on tile %lu %lu %lu\n", lvl,y,x);

					//if (!dset[tid]->getTile(tile[tid], coord.z(), coord.y(), coord.x())) {
					if (!dset[tid]->getTile(tileImages[tid], coord.z(), coord.y(), coord.x())) {

						//if (tid == 0) { cv::imshow("tile", tile[tid]); cv::waitKey(0); }

						WritableTile &outTile = outDset.blockingGetTileBufferForThread(tid);

						{
							AtomicTimerMeasurement tg(t_encodeImage);
							encode(outTile.eimg, tileImages[tid]);
						}

						outTile.coord = coord;
						outDset.sendCommand({DbCommand::TileReady, outTile.bufferIdx});
						tilesInRow++;
				}
			}

			int tid = omp_get_thread_num();
			//if (tid == 0) {
			if (true) {
				float yyy = y - tileTlbr[1];
				auto endTime = std::chrono::high_resolution_clock::now();
				double seconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime-startTime).count() * 1e-9;
				double tps = tilesInRow / seconds;
				printf(" - ~%.2f%% finished (row %d / %d, %d tiles, %.2f tile/sec)\n", 100.f * yyy / nrows, y-tileTlbr[1], nrows, tilesInRow, tps); fflush(stdout);
			}
		}

		outDset.blockUntilEmptiedQueue();
		outDset.sendCommand(DbCommand{DbCommand::EndLvl, lvl});
		outDset.blockUntilEmptiedQueue();


		uint64_t finalTlbr[4];
		uint64_t nHere = outDset.determineLevelAABB(finalTlbr, lvl);
		uint64_t nExpected = (finalTlbr[2]-finalTlbr[0]) * (finalTlbr[3]-finalTlbr[1]);
		uint64_t nExpectedInput = (1+tileTlbr[2]-tileTlbr[0]) * (1+tileTlbr[3]-tileTlbr[1]);
		printf(" - Final Tlbr on Lvl %d:\n", lvl);
		printf(" -        %lu %lu -> %lu %lu\n", finalTlbr[0], finalTlbr[1], finalTlbr[2], finalTlbr[3]);
		printf(" -        %lu tiles (%lu missing interior, %lu missing from input aoi)\n", nHere, nExpected-nHere, nExpectedInput-nHere);
	}


	{
		printf(" - Recomputing Dataset Meta.\n");
		MDB_txn* txn;
		outDset.beginTxn(&txn, false);
		outDset.recompute_meta_and_write_slow(txn);
		outDset.endTxn(&txn);
	}

	printDebugTimes();

	return 0;
}

int main(int argc, char** argv) {

	//test1();
	//test2();
	//return 0;

	if (argc <= 3) {
		printf("\n - Usage:\n\tconvertGdal <src.tif> <outPath> <format> <lvls>+ --subaoi <tlbr_wm>\n");
		if (argc == 3) printf("\t(You must provide at least one level.)\n");
		return 1;
	}

	// If zeros, ignored. If user enters subaoi, then only this aoi is used (not full tiff).
	// Should be specified in WebMercator (not unit)
	double overrideTlbr[4] = {0,0,0,0};

	std::string srcTiff, outPath;
	srcTiff = std::string(argv[1]);
	outPath = std::string(argv[2]);
	std::vector<int> lvls;

	AtomicTimerMeasurement _tg_total(t_total);

	std::string fmt = std::string{argv[3]};
	Image::Format outFormat;
	if (fmt == "gray") outFormat = Image::Format::GRAY;
	else if (fmt == "rgb") outFormat = Image::Format::RGB;
	else if (fmt == "rgba") outFormat = Image::Format::RGBA;
	else if (fmt == "terrain") outFormat = Image::Format::TERRAIN_2x8;
	else throw std::runtime_error("unk format: " + fmt + ", must be GRAY/RGB/RGBA/TERRAIN_2x8");

	// try {
		for (int i=4; i<argc; i++) {

			if (strcmp(argv[i],"--subaoi") == 0) {
				i++;
				overrideTlbr[0] = std::stof(argv[i++]);
				overrideTlbr[1] = std::stof(argv[i++]);
				overrideTlbr[2] = std::stof(argv[i++]);
				overrideTlbr[3] = std::stof(argv[i++]);
				break;
			} else {

				lvls.push_back( std::stoi(argv[i]) );
				if (lvls.back() < 0 or lvls.back() >= 27) {
					printf(" - Lvls must be >0 and <28\n");
					return 1;
				}
			}
		}
	// } catch (...) { printf(" - You provided a non-integer or invalid level.\n"); return 1; }

	return test3(srcTiff, outPath, lvls, outFormat, overrideTlbr);
}
