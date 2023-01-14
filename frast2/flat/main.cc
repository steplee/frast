
#include "writer.h"

using namespace frast;

int main() {


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

	{
		EnvOptions envOpts;
		envOpts.isTerrain = isTerrain;
		WriterMaster wm(fname, envOpts);
		wm.start(cfg);

		while (not wm.didWriterLoopExit())
			sleep(1);
		fmt::print(" - main detected base level finished, stopping.\n");
		wm.stop();
	}

	if (cfg.addo) {
		EnvOptions envOpts;
		envOpts.isTerrain = isTerrain;
		WriterMasterAddo wm(fname, envOpts);
		wm.start(cfg);

		while (not wm.didWriterLoopExit())
			sleep(1);
		fmt::print(" - main detected addo finished, stopping.\n");
		wm.stop();
	}


	fmt::print(" - you may want to run 'e4defrag' on the output file.\n");

	return 0;
}
