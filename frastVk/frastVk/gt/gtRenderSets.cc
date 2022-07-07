#include "frastVk/gt/gt.h"
#include "frastVk/gt/rt/rt.h"
#include "frastVk/extra/headlessCopyHelper.hpp"


class GenSetsApp : public BaseApp, public HeadlessCopyHelper<GenSetsApp> {

		std::shared_ptr<RtRenderer> rtr;

		inline virtual void initVk() {
			BaseApp::initVk();


			rtr = std::make_shared<RtRenderer>(rtCfg);
		}

		inline virtual void handleCompletedHeadlessRender(RenderState& rs, FrameData& fd, ExImage* depthImagePtr) override {
			helper_handleCompletedHeadlessRender(rs, fd, &simpleRenderPass.depthImages[fd.scIndx]);
		}
};

void run_rt(std::vector<std::string> args) {
	GenSetsApp renderer;
}


int main(int argc, char** argv) {

	std::vector<std::string> args;
	for (int i=0; i<argc; i++) args.push_back(std::string{argv[i]});

	run_rt(args);

	return 0;
}
