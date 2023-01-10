
#include "writer.h"

using namespace frast;

int main() {

	std::string fname = "flat.it";
	unlink(fname.c_str());

	ConvertConfig cfg;
	cfg.srcPaths = {"/data/naip/mocoNaip/whole.wm.tif"};
	cfg.baseLevel = 15;
	cfg.addo = true;

	{
		EnvOptions envOpts;
		WriterMaster wm(fname, envOpts);
		wm.start(cfg);

		while (not wm.didWriterLoopExit())
			sleep(1);
		fmt::print(" - main detected base level finished, stopping.\n");
		wm.stop();
	}

	if (cfg.addo) {
		EnvOptions envOpts;
		WriterMasterAddo wm(fname, envOpts);
		wm.start(cfg);

		while (not wm.didWriterLoopExit())
			sleep(1);
		fmt::print(" - main detected addo finished, stopping.\n");
		wm.stop();
	}



	return 0;
}
