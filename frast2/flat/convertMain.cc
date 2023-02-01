#include "writer.h"
#include "frast2/detail/argparse.hpp"

using namespace frast;

int main(int argc, char** argv) {

	setenv("VRT_SHARED_SOURCE", "0", false);

	/*
	std::string fname = "/data/naip/mocoNaip/moco.fft";
	unlink(fname.c_str());
	ConvertConfig cfg;
	cfg.srcPaths = {"/data/naip/mocoNaip/whole.wm.tif"};
	cfg.baseLevel = 15;
	cfg.addo = true;
	bool isTerrain = false;

	if (0) {
		fname = "/data/elevation/gmted/gmted.fft";
		unlink(fname.c_str());
		cfg.srcPaths = {"/data/elevation/gmted/usa_mean150.3857_4979.tif"};
		cfg.baseLevel = 8;
		cfg.addo = true;
		isTerrain = true;
		cfg.channels = 1;
	}
	*/

	ArgParser parser(argc, argv);
	auto color = parser.getChoice2("-c", "--color", "rgb", "gray", "terrain").value();
	std::string inpPath = parser.get2<std::string>("-i", "--input").value();
	std::string outPath = parser.get2<std::string>("-o", "--output").value();
	int level = parser.get2<int>("-l", "--level").value();

	auto threads_ = parser.get<int>("--threads");
	int threads = FRAST_WRITER_THREADS;
	if (threads_) {
		threads = threads_.value();
		if (threads <= 0) throw std::runtime_error(fmt::format("given threads ({}) should be >0 and <=FRAST_WRITER_THREADS ({})", threads, FRAST_WRITER_THREADS));
		if (threads > FRAST_WRITER_THREADS) throw std::runtime_error(fmt::format("given threads ({}) should be >0 and <=FRAST_WRITER_THREADS ({})", threads, FRAST_WRITER_THREADS));
	}

	struct stat statbuf;
	int res = ::stat(outPath.c_str(), &statbuf);
	if (res == 0) {
		// unlink(outPath.c_str());
		fmt::print(" - Not running: the output file '{}' already exists\n", outPath);
		throw std::runtime_error("output file already exists");
	}

	EnvOptions envOpts;
	ConvertConfig ccfg;

	if (color == "terrain") {
		envOpts.isTerrain = true;
		ccfg.channels = 1;
	} else if (color == "gray") {
		ccfg.channels = 1;
	} else if (color == "rgb") {
		ccfg.channels = 3;
	} else {
		throw std::runtime_error("unsupported 'color' option");
	}

	ccfg.srcPaths = {inpPath};
	ccfg.baseLevel = level;
	ccfg.addo = true;


	// Run initial job: convert gdal -> frast2
	{
		WriterMaster wm(outPath, envOpts, threads);
		wm.start(ccfg);

		while (not wm.didWriterLoopExit())
			sleep(1);
		fmt::print(" - main detected base level finished, stopping.\n");
		wm.stop();
	}

	// Run addo job: convert frast2 -> frast2
	//               half-scaling each level until the the range stops getting smaller (1x1 or so).
	if (ccfg.addo) {
		WriterMasterAddo wm(outPath, envOpts);
		wm.start(ccfg);

		while (not wm.didWriterLoopExit())
			sleep(1);
		fmt::print(" - main detected addo finished, stopping.\n");
		wm.stop();
	}


	fmt::print(" - you may want to run 'e4defrag' on the output file.\n");

	return 0;
}
