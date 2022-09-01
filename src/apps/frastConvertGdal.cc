#include "frastConvertGdal.hpp"

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

static int run_it(const std::string& srcTiff, const std::string& outPath, std::vector<int>& lvls, const Image::Format& outFormat, double overrideTlbr[4]) {
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
