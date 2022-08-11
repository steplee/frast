#include "frastConvertGdal.hpp"

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

	//Image dst { out.rows, out.cols, channels, out.data };
	Image& src = *srcImg;
	Image& dst = out;
	{
		// Debugged the streaks here; fount out the VRT shared issue
		// imshow("thread" + std::to_string(0), src); usleep(1'000'000);

		// AtomicTimerMeasurement tg(t_warp);
		src.remapRemap(dst, meshPtsf.data(), rtN, rtN);
	}

	return false;
}

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

static int run_it(const std::string& srcTiff, const std::string& outPath, std::vector<int>& lvls, Image::Format& outFormat, double overrideTlbr[4]) {
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
							// AtomicTimerMeasurement tg(t_encodeImage);
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

	return 0;
}

int main(int argc, char** argv) {

	// With VRTs we need 'shared mode' disabled when using multiple threads.
	// This is the best way to set it (user can still override, but should not)
	//
	// I had a verrry strange issue on my first time using VRTs with this, where I got long
	// horizontal streaks.
	setenv("VRT_SHARED_SOURCE", "0", false);

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

	// AtomicTimerMeasurement _tg_total(t_total);

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

	return run_it(srcTiff, outPath, lvls, outFormat, overrideTlbr);
}
