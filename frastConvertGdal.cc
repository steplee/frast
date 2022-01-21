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

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include <Eigen/Core>
#include <Eigen/LU>
//#include <Eigen/Geometry>



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

struct GdalDset {
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	inline GdalDset() {}
	GdalDset(const std::string& path);
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

	bool bboxProj(const Vector4d& bboxProj, int outw, int outh, cv::Mat& out) const;
	bool getTile(cv::Mat& out, int z, int y, int x, int tileSize=256);
	cv::Mat remapBuf;
};

static std::once_flag flag__;
GdalDset::GdalDset(const std::string& path) {
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
    if (nbands == 3 and gdalType == GDT_Byte) cv_type = CV_8UC3, eleSize = 1;
	else if (nbands == 1 and gdalType == GDT_Byte) cv_type = CV_8UC1, eleSize = 1;
	else if (nbands == 1 and gdalType == GDT_Int16) cv_type = CV_16SC1, eleSize = 2;
	else if (nbands == 1 and gdalType == GDT_Float32) cv_type = CV_32FC1, eleSize = 4;
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
    double s = .5 / WebMercatorScale;
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



bool GdalDset::bboxProj(const Vector4d& bboxProj, int outw, int outh, cv::Mat& out) const {
    out.create(outh, outw, cv_type);

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
        return err != CE_None;
    } else {
        out = cv::Scalar{0};
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

bool GdalDset::getTile(cv::Mat& out, int z, int y, int x, int tileSize) {
	int ow = out.cols, oh = out.rows;

	constexpr bool debug = false;

	constexpr int rtN = 128;
	constexpr int N = rtN * rtN;
	assert(tileSize % rtN == 0);

	bool y_flipped = pix2prj(1,1) < 0;
	double sampleScale = 2; // I'd say 2 looks best. It helps reduce interp effects. 1 is a little blurry.
	int sw = out.cols*sampleScale+.1, sh = out.rows*sampleScale+.1;

	// Start off as wm, but transformed to prj
	RowMatrix2Xd pts { 2 , N };
	{
		double start = 0;
		double end   = 1.;
		//start = .5 / rtN;
		//end   = 1. - .5 / rtN;
		//start = 1. / rtN;
		//end   = 1. - 1. / rtN;
		double s = static_cast<double>(1 << z);
		double scale = WebMercatorScale / (s * .5);
		double off_x = (static_cast<double>(x)) * WebMercatorScale / s * 2 - WebMercatorScale;
		double off_y = (static_cast<double>(y)) * WebMercatorScale / s * 2 - WebMercatorScale;
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

	cv::Mat imgPrj;
	assert(cv_type == CV_8UC1 or cv_type == CV_8UC3);
	//imgPrj.create(oh,ow,cv_type);
	bool res = bboxProj(prjBbox, sw, sh, imgPrj);
	if (res) {
		//printf(" - Failed to get tile %d %d %d\n", z, y, x); fflush(stdout);
		return res;
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

	cv::Mat meshMat { rtN, rtN, CV_32FC2, meshPtsf.data() };
	if (meshMat.cols != ow or meshMat.rows != oh) {
	cv::resize(meshMat, meshMat, cv::Size{ow, oh}, 0,0, cv::INTER_LINEAR);
	//cv::resize(meshMat, meshMat, cv::Size{ow, oh}, 0,0, cv::INTER_CUBIC);
	}
	//std::cout << meshMat << "\n";

	//out.create(imgPrj.rows, imgPrj.cols, imgPrj.type());
	//cv::remap(imgPrj, out, meshMat, cv::noArray(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
	cv::remap(imgPrj, out, meshMat, cv::noArray(), cv::INTER_AREA, cv::BORDER_REPLICATE);

	return false;
}







bool encode_cv__(EncodedImage& out, const cv::Mat& mat) {
	AtomicTimerMeasurement g(t_encodeImage);
	cv::imencode(".jpg", mat, out);
	return false;
}






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
#define THREADS 4
	GdalDset* dset[THREADS];
	cv::Mat tile[THREADS];

	for (int i=0; i<THREADS; i++) {
		dset[i] = new GdalDset { "/data/naip/mocoNaip/whole.tif" };
		std::cout << " - dset ptr : " << dset[i]->dset << "\n";
		tile[i] = cv::Mat ( 256, 256, dset[i]->cv_type );
	}

	std::cout << " - Making level 16.\n";

	constexpr int N = 10;
	#pragma omp parallel for schedule(static,4) num_threads(THREADS)
	for (int y=0; y<N; y++)
	for (int x=0; x<N; x++) {
		int tid = omp_get_thread_num();
		dset[tid]->getTile(tile[tid], 20-4, 648536/16-15+y, 299283/16-15+x);
	}

	for (int i=0; i<THREADS; i++) delete dset[i];
#undef THREADS
}

static void findWmTlbrOfDataset(uint64_t tlbr[4], GdalDset* dset, int lvl) {
	double s = (1lu<<((int64_t)lvl));

	tlbr[0] = (uint64_t)(dset->tlbr_uwm[0] * s) + 4lu;
	tlbr[1] = (uint64_t)(dset->tlbr_uwm[1] * s) + 4lu;
	tlbr[2] = (uint64_t)(dset->tlbr_uwm[2] * s) - 4lu;
	tlbr[3] = (uint64_t)(dset->tlbr_uwm[3] * s) - 4lu;

	tlbr[0] = (uint64_t)(dset->tlbr_uwm[0] * s + .5) + 0lu;
	tlbr[1] = (uint64_t)(dset->tlbr_uwm[1] * s + .5) + 0lu;
	tlbr[2] = (uint64_t)(dset->tlbr_uwm[2] * s     ) - 0lu;
	tlbr[3] = (uint64_t)(dset->tlbr_uwm[3] * s     ) - 0lu;

}

#define THREADS 4
static int test3(const std::string& srcTiff, const std::string& outPath, std::vector<int>& lvls) {
	// Open one DB, and one tiff+buffer per thread

	GdalDset* dset[THREADS];
	cv::Mat img[THREADS];
	std::vector<uint8_t> tmpBuf[THREADS];
	//TileImage tileImages[THREADS];
	Image tileImages[THREADS];
	cv::Mat tile[THREADS];

	for (int i=0; i<THREADS; i++) {
		dset[i] = new GdalDset { "/data/naip/mocoNaip/whole.tif" };
		std::cout << " - dset ptr : " << dset[i]->dset << "\n";
		//tileImages[i] = TileImage { 256, 256, dset[i]->nbands };
		//tileImages[i].isOverview = false;
		tileImages[i] = Image { 256, 256, dset[i]->nbands };
		tileImages[i].alloc();
		tile[i] = cv::Mat ( 256, 256, dset[i]->cv_type, tileImages[i].buffer );
	}

	DatasetWritable outDset { outPath };
	outDset.configure(THREADS, 4);
	std::cout << " - beginning" << std::endl;

	// For each level
	//    Get bbox of dataset
	//    For each tile in bbox [multithreaded]
	//        getTile()
	//        putTile()

	for (int lvli = 0; lvli<lvls.size(); lvli++) {
		int lvl = lvls[lvli];

		outDset.sendCommand(Command{Command::BeginLvl,lvl});

		uint64_t tileTlbr[4];
		findWmTlbrOfDataset(tileTlbr, dset[0], lvl);

		int nrows = (tileTlbr[3]-tileTlbr[1]);
		int ncols = (tileTlbr[2]-tileTlbr[0]);
		int totalTiles = nrows*ncols;
		printf(" - Lvl %d:\n",lvl);
		printf(" -        %lu %lu -> %lu %lu\n", tileTlbr[0], tileTlbr[1], tileTlbr[2], tileTlbr[3]);
		printf(" -        %lu rows\n", nrows);
		printf(" -        %lu cols\n", ncols);

		#pragma omp parallel for schedule(static,4) num_threads(THREADS)
		for (uint64_t y=tileTlbr[1]; y<tileTlbr[3]; y++) {
			for (uint64_t x=tileTlbr[0]; x<tileTlbr[2]; x++) {

				int tid = omp_get_thread_num();
				BlockCoordinate coord { (uint64_t)lvl, y, x};
				//printf(" - on tile %lu %lu %lu\n", lvl,y,x);

					if (!dset[tid]->getTile(tile[tid], coord.z(), coord.y(), coord.x())) {

						//if (tid == 0) { cv::imshow("img",tile[tid]); cv::waitKey(1); }

						WritableTile &outTile = outDset.blockingGetTileBufferForThread(tid);

						//encode_cv__(outTile.eimg, tile[tid]);
						//encode(outTile.eimg, tmpBuf[tid], tileImages[tid]);
						encode(outTile.eimg, tileImages[tid]);

						outTile.coord = coord;
						//outDset.push(outTile);
						outDset.sendCommand({Command::TileReady, outTile.bufferIdx});
				}
			}

			int tid = omp_get_thread_num();
			if (tid == 0) {
				float yyy = y - tileTlbr[1];
				printf(" - ~%.2f%% finished (row %d / %d)\n", 100.f * yyy / nrows, y-tileTlbr[1], nrows); fflush(stdout);
			}
		}

		outDset.sendCommand(Command{Command::EndLvl, lvl});

		while (outDset.hasOpenWrite())
			usleep(10'000);

		uint64_t finalTlbr[4];
		uint64_t nHere = outDset.determineLevelAABB(finalTlbr, lvl);
		uint64_t nExpected = (finalTlbr[2]-finalTlbr[0]) * (finalTlbr[3]-finalTlbr[1]);
		uint64_t nExpectedInput = (tileTlbr[2]-tileTlbr[0]) * (tileTlbr[3]-tileTlbr[1]);
		printf(" - Final Tlbr on Lvl %d:\n", lvl);
		printf(" -        %lu %lu -> %lu %lu\n", finalTlbr[0], finalTlbr[1], finalTlbr[2], finalTlbr[3]);
		printf(" -        %lu tiles (%lu missing interior, %lu missing from input aoi)\n", nHere, nExpected-nHere, nExpectedInput-nHere);
	}

	printDebugTimes();

	return 0;
}
#undef THREADS

int main(int argc, char** argv) {

	//test1();
	//test2();
	//return 0;

	if (argc <= 3) {
		printf("\n - Usage:\n\tconvertGdal <src.tif> <outPath> <lvls>+\n");
		if (argc == 3) printf("\t(You must provide at least one level.)\n");
		return 1;
	}

	std::string srcTiff, outPath;
	srcTiff = std::string(argv[1]);
	outPath = std::string(argv[2]);
	std::vector<int> lvls;

	AtomicTimerMeasurement _tg_total(t_total);

	try {
		for (int i=3; i<argc; i++) {
			lvls.push_back( std::stoi(argv[i]) );
			if (lvls.back() < 0 or lvls.back() >= 27) {
				printf(" - Lvls must be >0 and <28\n");
				return 1;
			}
		}
	} catch (...) { printf(" - You provided a non-integer or invalid level.\n"); return 1; }

	return test3(srcTiff, outPath, lvls);
}
