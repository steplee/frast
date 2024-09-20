#include "dataset.h"
#include "frast2/detail/solve.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/core/types_c.h>
// #include <opencv2/highgui.hpp>

#include <algorithm>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include "../ftr/conversions.hpp"

namespace {
// Convert the int16_t -> uint16_t.
// Change nodata value from -32768 to 0. Clamp min to 0.
// Clamp max to 8191, then multiply by 8.
// The final mult is done to get 8 ticks/meter value-precision, so that warps have higher accuracy.
// That means that the user should convert to float, then divide by 8 after accessing the dataset.
void transform_gmted(uint16_t* buf, int h, int w) {
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			int16_t gmted_val = ((int16_t*)buf)[y * w + x];

			if (gmted_val < 0) gmted_val = 0;
			if (gmted_val > 8191) gmted_val = 8191;

			uint16_t val = gmted_val * 8;

			buf[y * w + x] = val;
		}
}

std::once_flag flag__;
}  // namespace

namespace frast {

GdalDataset::GdalDataset(const std::string& path, bool isTerrain) : isTerrain(isTerrain) {
	std::call_once(flag__, &GDALAllRegister);

	dset = (GDALDataset*)GDALOpen(path.c_str(), GA_ReadOnly);
	if (dset == nullptr) throw std::runtime_error("failed to open dataset: " + path);

	nbands = dset->GetRasterCount() >= 3 ? 3 : 1;
	assert(nbands <= 4);
	auto band = dset->GetRasterBand(0 + 1);
	for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i);
	// std::cout << " - nbands: " << nbands << "\n";

	gdalType = dset->GetRasterBand(1)->GetRasterDataType();
	// fmt::print(" - file {} gdalType {} '{}'\n", path, gdalType, GDALGetDataTypeName(gdalType));
	if (not(gdalType == GDT_Byte or gdalType == GDT_Int16 or gdalType == GDT_Float32)) {
		std::cerr << " == ONLY uint8_t/int16_t/float32 dsets supported right now." << std::endl;
		exit(1);
	}
	if (nbands == 3 and gdalType == GDT_Byte) {
		eleSize = 1;
	} else if (nbands == 1 and gdalType == GDT_Byte) {
		eleSize = 1;
	} else if (nbands == 1 and gdalType == GDT_Int16) {
		eleSize = 2;
	} else if (nbands == 1 and gdalType == GDT_Float32) {
		throw std::runtime_error(
			"geotiff is float32 input is not supported yet. I have yet to implement terrain conversion from f32 -> "
			"TERRAIN_2x8");
		eleSize = 4;
	} else assert(false);

	internalCvType = nbands == 1 ? CV_8UC1 : nbands == 3 ? CV_8UC3 : nbands == 4 ? CV_8UC4 : -1;
	assert(internalCvType != -1);
	if (isTerrain) {
		assert(gdalType == GDT_Int16);
		internalCvType = CV_16UC1;
	}

	if (not isTerrain) {
		int blockSizeX, blockSizeY;
		bands[0]->GetBlockSize(&blockSizeX, &blockSizeY);
		assert(blockSizeX == 256);
		assert(blockSizeY == 256);
	}

	// FIXME: Assert that it is WM projected.

	double g[6];
	dset->GetGeoTransform(g);
	RowMatrix3d native_from_pix_;
	native_from_pix_ << g[1], g[2], g[0], g[4], g[5], g[3], 0, 0, 1;
	RowMatrix3d pix_from_native_ = native_from_pix_.inverse();
	pix_from_native				 = pix_from_native_.topRows<2>();
	native_from_pix				 = native_from_pix_.topRows<2>();
	w							 = dset->GetRasterXSize();
	h							 = dset->GetRasterYSize();

	// Ensure that the dataset is projected and aligned to WM quadtree.
	if (not isTerrain) {
		double bestErr = 9e9;
		int matchedPixelLevel = -1;
		double matchedPixelSize = -1;
		Vector2d pt_a = native_from_pix * Vector3d {0,0,1};
		Vector2d pt_b = native_from_pix * Vector3d {1,0,1};
		double pixelScaleWm = (pt_b - pt_a).norm();
		for (int lvl=0; lvl<30; lvl++) {
			double levelCellScaleWm = (WebMercatorScale * 2) / (1 << lvl);
			double err = std::abs(levelCellScaleWm - pixelScaleWm);
			if (err < bestErr) {
				bestErr = err;
				matchedPixelLevel = lvl;
				matchedPixelSize = levelCellScaleWm;
			}
		}
		fmt::print(" - From:\n");
		fmt::print("          pixelScaleWm : {:.5f}\n", pixelScaleWm);
		fmt::print("          matched level: {}\n", matchedPixelLevel);
		fmt::print("             pixel size: {}, with err {}\n", matchedPixelSize, bestErr);
		fmt::print("              rel error: {}\n", bestErr/matchedPixelSize);
		assert(matchedPixelLevel != -1 && "The dataset must be aligned to the WM quadtree");
		assert(bestErr / matchedPixelSize <= .001 && "The dataset must be aligned to the WM quadtree");

		Vector4d tlbrWm;
		{
			RowMatrix42d pts;
			pts << (native_from_pix * Vector3d{0, 0, 1.}).transpose(),
				(native_from_pix * Vector3d{w, 0, 1.}).transpose(),
				(native_from_pix * Vector3d{w, h, 1.}).transpose(),
				(native_from_pix * Vector3d{0, h, 1.}).transpose();
			tlbrWm = Vector4d{pts.col(0).minCoeff(), pts.col(1).minCoeff(), pts.col(0).maxCoeff(), pts.col(1).maxCoeff()};
		}

		deepestLevelZ = matchedPixelLevel - 8;
		shallowestLevelZ = deepestLevelZ - bands[0]->GetOverviewCount() + 1;
		deepestLevelTlbr(0) = std::floor((tlbrWm(0) / WebMercatorScale + 1) * .5 * (1 << deepestLevelZ));
		deepestLevelTlbr(1) = std::floor((tlbrWm(1) / WebMercatorScale + 1) * .5 * (1 << deepestLevelZ));
		deepestLevelTlbr(2) = std::ceil ((tlbrWm(2) / WebMercatorScale + 1) * .5 * (1 << deepestLevelZ));
		deepestLevelTlbr(3) = std::ceil ((tlbrWm(3) / WebMercatorScale + 1) * .5 * (1 << deepestLevelZ));
		shallowestLevelTlbr(0) = std::floor((tlbrWm(0) / WebMercatorScale + 1) * .5 * (1 << shallowestLevelZ));
		shallowestLevelTlbr(1) = std::floor((tlbrWm(1) / WebMercatorScale + 1) * .5 * (1 << shallowestLevelZ));
		shallowestLevelTlbr(2) = std::ceil ((tlbrWm(2) / WebMercatorScale + 1) * .5 * (1 << shallowestLevelZ));
		shallowestLevelTlbr(3) = std::ceil ((tlbrWm(3) / WebMercatorScale + 1) * .5 * (1 << shallowestLevelZ));
	}




}

GdalDataset::~GdalDataset() {
	if (dset) GDALClose(dset);
	dset = 0;
}

int GdalDataset::getNumOverviews() const { return bands[0]->GetOverviewCount(); }

// NOTE: This could use `getPix` of course, but perhaps more efficient to use the
//       block-based functions?
cv::Mat GdalDataset::getLocalTile(int x, int y, int overview) {
	cv::Mat img(256, 256, internalCvType);

	int bandsToUse = nbands == 1 ? 1 : (nbands == 3 or nbands == 4) ? 3 : -1;
	int C		   = img.channels();
	// fmt::print("b {} {} c {}\n", nbands, bandsToUse, C);
	assert(bandsToUse == 1 or bandsToUse == 3);

	std::vector<uint8_t> bytes(256 * 256);

	assert(C == bandsToUse);

	int H = 256;
	int W = 256;

	for (int i = 0; i < bandsToUse; i++) {
		auto bandOvr = overview == 0 ? bands[i] : bands[i]->GetOverview(overview - 1);
		bandOvr->ReadBlock(x, y, bytes.data());
		for (int y = 0; y < H; y++) {
			for (int x = 0; x < W; x++) {
				for (int c = 0; c < bandsToUse; c++) { img.data[y * W * C + x * C + c] = bytes[y * W + x]; }
			}
		}
	}

	return img;
}


cv::Mat GdalDataset::getGlobalTile(int x, int y, int z) {

	int dz = deepestLevelZ - z;
	int scale = 1 << dz;

	int lx = x - deepestLevelTlbr(0)/scale;


	int ly = deepestLevelTlbr(3)/scale - y - 1;

	lx += deepestLevelTlbr(0)/scale - shallowestLevelTlbr(0) * (1 << (z - shallowestLevelZ));
	// ly -= deepestLevelTlbr(3)/scale - shallowestLevelTlbr(3) * (1 << (z - shallowestLevelZ));
	ly = ((shallowestLevelTlbr(3)-1) * (1 << (z - shallowestLevelZ))) - y - 1;

	/*
			ly = (deepestLevelTlbr(3))/scale - y - 1;
			ly += (deepestLevelTlbr(3)+scale-1)/scale - shallowestLevelTlbr(3) * (1 << (z - shallowestLevelZ));
			ly = - ly;
	*/
	int ovr = dz;


	// ly = ((h + 255) / 256 - 1)/scale - ly;

	fmt::print("getGlobalTile ovr {}, deepestZ {}, z {}, dz {}\n", ovr, deepestLevelZ, z, dz);
	return getLocalTile(lx,ly,ovr);
}

BlockCoordinate GdalDataset::localToGlobalCoord(int x, int y, int ovr) {
	int z = deepestLevelZ - ovr;
	int scale = 1 << (deepestLevelZ - z);

	// x -= deepestLevelTlbr(0)/scale - shallowestLevelTlbr(0) * (1 << (z - shallowestLevelZ));
	// y -= deepestLevelTlbr(1)/scale - shallowestLevelTlbr(1) * (1 << (z - shallowestLevelZ));


	// y = ((h + 255) / 256) / scale - 1 - y;

	return BlockCoordinate {
		z,
		// y + deepestLevelTlbr(1) / scale,
		deepestLevelTlbr(3) / scale - 1 - y,
		x + deepestLevelTlbr(0) / scale
	};
}

Vector4d GdalDataset::getWm(const Vector4d& tlbrWm, cv::Mat& out) {
	RowMatrix42d pts;
	pts << (pix_from_native * Vector3d{tlbrWm(0), tlbrWm(1), 1.}).transpose(),
		(pix_from_native * Vector3d{tlbrWm(2), tlbrWm(1), 1.}).transpose(),
		(pix_from_native * Vector3d{tlbrWm(2), tlbrWm(3), 1.}).transpose(),
		(pix_from_native * Vector3d{tlbrWm(0), tlbrWm(3), 1.}).transpose();
	Vector4d tlbrPix{pts.col(0).minCoeff(), pts.col(1).minCoeff(), pts.col(0).maxCoeff(), pts.col(1).maxCoeff()};
	return getPix(tlbrPix, out);
}
Vector4d GdalDataset::getLocalTileBoundsWm(int x, int y, int overview) {

	auto bc = localToGlobalCoord(x,y,overview);
	return getGlobalTileBoundsWm(bc.x(), bc.y(), bc.z());

	/*
	double s = 1 << overview;

	RowMatrix42d pts;
	pts.row(0) = (native_from_pix * Vector3d{(x) * 256 * s, (y) * 256 * s, 1}).transpose();
	pts.row(1) = (native_from_pix * Vector3d{(x + 1) * 256 * s, (y) * 256 * s, 1}).transpose();
	pts.row(2) = (native_from_pix * Vector3d{(x + 1) * 256 * s, (y + 1) * 256 * s, 1}).transpose();
	pts.row(3) = (native_from_pix * Vector3d{(x) * 256 * s, (y + 1) * 256 * s, 1}).transpose();

	Vector4d tlbrWm{pts.col(0).minCoeff(), pts.col(1).minCoeff(), pts.col(0).maxCoeff(), pts.col(1).maxCoeff()};

	return tlbrWm;
	*/
}
Vector4d GdalDataset::getGlobalTileBoundsWm(int x, int y, int z) {
	// int zz = z - 8;
	int zz = z;
	// fmt::print("getGlobalTileBoundsWm: {} {} {}\n", x,y,z);
	return Vector4d {
		(static_cast<double>(x  ) / (1 << zz) * 2 - 1) * WebMercatorScale,
		(static_cast<double>(y  ) / (1 << zz) * 2 - 1) * WebMercatorScale,
		(static_cast<double>(x+1) / (1 << zz) * 2 - 1) * WebMercatorScale,
		(static_cast<double>(y+1) / (1 << zz) * 2 - 1) * WebMercatorScale
	};
}

Vector4d GdalDataset::getPix(const Vector4d& tlbrPix, cv::Mat& out) {
	int outh = out.rows, outw = out.cols;

	int out_c = out.channels();
	if (nbands >= 3 and out_c == 1) out.create(outh, outw, CV_8UC3);

	Vector2d tl = tlbrPix.head<2>();
	Vector2d br = tlbrPix.tail<2>();
	if (tl(0) > br(0)) std::swap(tl(0), br(0));
	if (tl(1) > br(1)) std::swap(tl(1), br(1));
	int xoff = tl(0);
	int yoff = tl(1);
	// int xsize = (int)(.5 + br(0) - tl(0));
	// int ysize = (int)(.5 + br(1) - tl(1));
	int xsize = (int)(br(0) - tl(0));
	int ysize = (int)(br(1) - tl(1));
	if (xsize == 0) xsize++;
	if (ysize == 0) ysize++;

	if (xoff > 0 and xoff + xsize < w and yoff > 0 and yoff + ysize < h) {
		// FIXME: use greater precision with bFloatingPointWindowValidity

		GDALRasterIOExtraArg arg;
		arg.nVersion = RASTERIO_EXTRA_ARG_CURRENT_VERSION;
		if (bilinearSampling) arg.eResampleAlg = GRIORA_Bilinear;
		else arg.eResampleAlg = GRIORA_NearestNeighbour;
		arg.pfnProgress	  = 0;
		arg.pProgressData = 0;

		if (useSubpixelOffsets) {
			arg.dfXOff = tl(0);
			arg.dfYOff = tl(1);
			arg.dfXSize = br(0) - tl(0);
			arg.dfYSize = br(1) - tl(1);
			arg.bFloatingPointWindowValidity = 1;
			fmt::print(" - subpixel: {} {} {} {} \n", arg.dfXOff, arg.dfYOff, arg.dfXSize, arg.dfYSize);
		} else {
			arg.bFloatingPointWindowValidity = 0;
		}

		auto err = dset->RasterIO(GF_Read, xoff, yoff, xsize, ysize, out.data, outw, outh, gdalType, nbands, nullptr,
								  eleSize * nbands, eleSize * nbands * outw, eleSize, &arg);
		// fmt::print(" - err: {}\n", err);

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain) transform_gmted((uint16_t*)out.data, outh, outw);

		/*
		if (isTerrain and out.type() == CV_32FC1) {
			out.flags = CV_16UC1;
			out.convertTo(out, CV_32FC1, 1./8.);
		}
		*/

		if (err != CE_None) return Vector4d::Zero();

	} else if (xoff + xsize >= 1 and xoff < w and yoff + ysize >= 1 and yoff < h) {
		// case where there is partial overlap
		// WARNING: This is not correct. Border tiles have artifacts at corners.
		Eigen::Vector4i inner{std::max(0, xoff), std::max(0, yoff), std::min(w - 1, xoff + xsize),
							  std::min(h - 1, yoff + ysize)};
		float			sx		= ((float)outw) / xsize;
		float			sy		= ((float)outh) / ysize;
		int				inner_w = inner(2) - inner(0), inner_h = inner(3) - inner(1);
		int				read_w = (inner(2) - inner(0)) * sx, read_h = (inner(3) - inner(1)) * sy;
		// printf(" - partial bbox: %dh %dw %dc\n", read_h, read_w, out.channels()); fflush(stdout);
		if (read_w <= 0 or read_h <= 0) return Vector4d::Zero();

		cv::Mat tmp;
		tmp.create(read_h, read_w, internalCvType);

		auto err = dset->RasterIO(GF_Read, inner(0), inner(1), inner_w, inner_h, tmp.data, read_w, read_h, gdalType,
								  nbands, nullptr, eleSize * nbands, eleSize * nbands * read_w, eleSize * 1, nullptr);
		if (err != CE_None) return Vector4d::Zero();

		// TODO If converting from other terrain then GMTED, must modify here
		if (isTerrain) transform_gmted((uint16_t*)tmp.data, tmp.rows, tmp.cols);

		float in_pts[8]	 = {0, 0, sx * inner_w, 0, 0, sy * inner_h, sx * inner_w, sy * inner_h};
		float out_pts[8] = {sx * (inner(0) - xoff), sy * (inner(1) - yoff), sx * (inner(2) - xoff),
							sy * (inner(1) - yoff), sx * (inner(0) - xoff), sy * (inner(3) - yoff),
							sx * (inner(2) - xoff), sy * (inner(3) - yoff)};

		// xoff = inner(0); yoff = inner(1);
		// xsize = inner_w; ysize = inner_h;

		float H[9];
		solveHomography(H, in_pts, out_pts);

		cv::Mat HH(3, 3, CV_32F, H);
		cv::warpPerspective(tmp, out, HH, cv::Size{out.cols, out.rows});

	} else {
		// memset(out.data, 0, out.rows*out.cols*out.channels());
		out = cv::Scalar{0};
		return Vector4d::Zero();
	}

	Vector2d tl_sampled = Vector2d{xoff, yoff};
	// Vector2d br_sampled = Vector2d { xoff+xsize, yoff+ysize };
	Vector2d br_sampled = Vector2d{xoff + xsize - 1, yoff + ysize - 1};
	// Vector2d br_sampled = Vector2d { xoff+xsize+1, yoff+ysize+1 };

	// Vector2d tl_sampled = Vector2d { xoff-.5, yoff-.5 };
	// Vector2d br_sampled = Vector2d { xoff+xsize+.5, yoff+ysize+.5 };

	// Vector2d br_sampled = pix2prj * Vector3d { xoff+xsize-1, yoff+ysize-1, 1 };
	if (tl_sampled(0) > br_sampled(0)) std::swap(tl_sampled(0), br_sampled(0));
	if (tl_sampled(1) > br_sampled(1)) std::swap(tl_sampled(1), br_sampled(1));
	return Vector4d{tl_sampled(0), tl_sampled(1), br_sampled(0), br_sampled(1)};
}

bool GdalDataset::haveLocalTile(int x, int y, int overview) {
	// if (overview < 0 or overview > bands[0]->GetOverviewCount() - 1) return false;
	if (overview < 0 or overview > bands[0]->GetOverviewCount() + 1) return false;
	auto band = overview == 0 ? bands[0] : bands[0]->GetOverview(overview - 1);
	int blockW = 0, blockH = 0;
	auto err = band->GetActualBlockSize(x,y, &blockW, &blockH);
	if (err != CE_None) return false;
	return blockW > 0 and blockH > 0;
}

}  // namespace frast
