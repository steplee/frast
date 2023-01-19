#include <fmt/core.h>

#include <cassert>
#include <cerrno>
#include <gdal_priv.h>
#include <gdal.h>
#include <gdalwarper.h>
#include <ogr_core.h>
#include <ogr_spatialref.h>

#include <chrono>

#include "frast2/flat/reader.h"
#include "frast2/flat/codec.h"

// Hmmmm. Gdal is faster with and without the actual histogram summation.
// Tried a bunch of things to speed up.
// Will have to see if it is due to opencv jpeg codec.

struct GdalRunner {

	GDALDataset* dset = nullptr;
	int nbands;
	GDALRasterBand* bands[4];
	GDALDataType gdalType;

	std::vector<std::vector<uint32_t>> histo;

	GdalRunner(const std::string& filename) {
		dset = (GDALDataset*) GDALOpen(filename.c_str(), GA_ReadOnly);
		nbands    = dset->GetRasterCount() >= 3 ? 3 : 1;
		auto band = dset->GetRasterBand(0 + 1);
		gdalType = dset->GetRasterBand(1)->GetRasterDataType();
		assert(gdalType == GDT_Byte);

		histo.resize(nbands);
		for (int i=0; i<nbands; i++) histo[i].resize(256);

		// for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i);
		for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i)->GetOverview(0);
		// for (int i = 0; i < nbands; i++) bands[i] = dset->GetRasterBand(1 + i)->GetOverview(1);
	}
	~GdalRunner() {
		delete ((GDALDataset*)dset);
	}

	void run() {

		auto st = std::chrono::high_resolution_clock::now();

		// for (int i=0; i<nbands; i++) GetHistogram_1(bands[i], histo[i].data());
		int nb = GetHistogram_2();

		auto et = std::chrono::high_resolution_clock::now();

		double t = std::chrono::duration_cast<std::chrono::microseconds>(et - st).count() * 1e-6;
		fmt::print(" - {} blocks took {}s, {}b/s, {}ms/b\n", nb, t, nb/t, t*1e6/nb);

	}

	// https://gdal.org/api/gdalrasterband_cpp.html#classGDALRasterBand_1a75d4af97b3436a4e79d9759eedf89af4
	/*
	int GetHistogram_1( GDALRasterBand *poBand, uint32_t *panHistogram )
	{
		memset( panHistogram, 0, sizeof(int) * 256 );
		CPLAssert( poBand->GetRasterDataType() == GDT_Byte );
		int nXBlockSize, nYBlockSize;

		poBand->GetBlockSize( &nXBlockSize, &nYBlockSize );
		int nXBlocks = (poBand->GetXSize() + nXBlockSize - 1) / nXBlockSize;
		int nYBlocks = (poBand->GetYSize() + nYBlockSize - 1) / nYBlockSize;

		GByte *pabyData = (GByte *) CPLMalloc(nXBlockSize * nYBlockSize);
		int nb = 0;

		for( int iYBlock = 0; iYBlock < nYBlocks; iYBlock++ )
		{
			for( int iXBlock = 0; iXBlock < nXBlocks; iXBlock++ )
			{
				int        nXValid, nYValid;

				poBand->ReadBlock( iXBlock, iYBlock, pabyData );

				// Compute the portion of the block that is valid
				// for partial edge blocks.
				poBand->GetActualBlockSize(iXBlock, iYBlock, &nXValid, &nYValid);
				nb ++;

					// Collect the histogram counts.
					for( int iY = 0; iY < nYValid; iY++ )
					{
						for( int iX = 0; iX < nXValid; iX++ )
						{
							panHistogram[pabyData[iX + iY * nXBlockSize]] += 1;
						}
					}
			}
		}
		return nb;
	}
	*/

	int GetHistogram_2()
	{

		auto poBand = bands[0];
		CPLAssert( poBand->GetRasterDataType() == GDT_Byte );
		int nXBlockSize, nYBlockSize;

		poBand->GetBlockSize( &nXBlockSize, &nYBlockSize );
		int nXBlocks = (poBand->GetXSize() + nXBlockSize - 1) / nXBlockSize;
		int nYBlocks = (poBand->GetYSize() + nYBlockSize - 1) / nYBlockSize;

		GByte *pabyData = (GByte *) CPLMalloc(nXBlockSize * nYBlockSize);
		int nb = 0;

		for( int iYBlock = 0; iYBlock < nYBlocks; iYBlock++ )
		{
			// fmt::print("row {} {}\n", iYBlock, nYBlocks);
			for( int iXBlock = 0; iXBlock < nXBlocks; iXBlock++ )
			{
				int        nXValid, nYValid;

				for (int c=0; c<nbands; c++) {
					bands[c]->ReadBlock( iXBlock, iYBlock, pabyData );

					// Compute the portion of the block that is valid
					// for partial edge blocks.
					bands[c]->GetActualBlockSize(iXBlock, iYBlock, &nXValid, &nYValid);

					if (nXValid > 0 or nYValid > 0)
						nb++;

					// fmt::print("valid {} {}\n", nYValid, nXValid);

					// Collect the histogram counts.
					/*
					for( int iY = 0; iY < nYValid; iY++ )
					{
						for( int iX = 0; iX < nXValid; iX++ )
						{
							histo[c][pabyData[iX + iY * nXBlockSize]] += 1;
						}
					}
					*/
				}
			}
		}
		return nb;
	}

};




using namespace frast;

struct FrastRunner {
	std::vector<std::vector<uint32_t>> histo;

	FlatReader* reader;

	FrastRunner(const std::string& filename) {
		EnvOptions eopts;
		eopts.readonly = true;
		reader = new FlatReader(filename, eopts);

		histo.resize(4);
		for (int i=0; i<4; i++) histo[i].resize(256);

	}
	~FrastRunner() {
		delete reader;
	}

	void run() {

		auto st = std::chrono::high_resolution_clock::now();

		// for (int i=0; i<nbands; i++) GetHistogram_1(bands[i], histo[i].data());
		int nb = compute_histogram();

		auto et = std::chrono::high_resolution_clock::now();

		double t = std::chrono::duration_cast<std::chrono::microseconds>(et - st).count() * 1e-6;
		fmt::print(" - {} blocks took {}s, {}b/s, {}ms/b\n", nb, t, nb/t, t*1e6/nb);

	}

	int compute_histogram() {
		int nb = 0;

		int lvl = 29;
		while (not reader->env.haveLevel(lvl)) lvl--;
		// lvl--;
		assert(lvl > 1);

		// inline uint64_t* getKeys(int lvl) {
		// inline LevelSpec& getLevelSpec(int lvl) {
		auto keys = reader->env.getKeys(lvl);
		auto &spec = reader->env.getLevelSpec(lvl);

		cv::Mat img;

		int n = spec.nitemsUsed();

		for (int i=0; i<n; i++) {
			auto key = keys[i];
			nb++;

			// cv::Mat img = reader->getTile(key, 3);

			Value v = reader->env.getValueFromIdx(lvl, i);
			{
				void* vv = v.value;
				vv = (void*)((((uint64_t)vv) >> 12) << 12);
				int err = madvise(vv, 1<<14, MADV_WILLNEED);
				// fmt::print(" - v {} e {} {}\n", vv, err, strerror(errno));
				// assert(err == 0);
			}
			// cv::Mat img = decodeValue(v, 3, false);

			// Version sharing allocations
			decodeValue(img, v, 3, false);
			
			int C = img.channels();

			/*
			for (int y=0; y<img.rows; y++) {
				for (int x=0; x<img.cols; x++) {
					for (int c=0; c<C; c++) {
						uint8_t i = static_cast<uint8_t*>(img.data)[y*img.cols*C+x*C+c];
						// histo[c][i] += 1;
						histo.data()[c].data()[i]++;
					}
				}
			}
			*/

			/*
			for (int y=0; y<img.rows*img.cols*C; y+=3) {
				histo[0][img.data[y+0]] += 1;
				histo[1][img.data[y+1]] += 1;
				histo[2][img.data[y+2]] += 1;
			}
			*/
		}




		return nb;
	}


};


/*


   To get a flame graph:

sudo /opt/linux/tools/perf/perf record -F max -a -g  -- ./benchmarkIteration
sudo /opt/linux/tools/perf/perf script > out.perf; ../FlameGraph/stackcollapse-perf.pl out.perf > out.folded; ../FlameGraph/flamegraph.pl out.folded > it.svg
# Then open in chrome

# In one line:
 sudo /opt/linux/tools/perf/perf record -F max -g --call-graph dwarf -- ./benchmarkIteration; sudo /opt/linux/tools/perf/perf script > out.perf; ../FlameGraph/stackcollapse-perf.pl out.perf > out.folded; ../FlameGraph/flamegraph.pl out.folded > it2.svg



   */


int main() {

	if (1){
		GDALAllRegister();

		GdalRunner gdalRunner { "/data/naip/mocoNaip/whole.wm.tif" };
		gdalRunner.run();
	}

	if (0){
		FrastRunner frastRunner { "/data/naip/mocoNaip/moco.fft" };
		frastRunner.run();
	}

	if (1){
		FrastRunner frastRunner { "/data/naip/mocoNaip/mocoDefrag.fft" };
		frastRunner.run();
	}

	return 0;
}
