#include "flat_env.h"

namespace cv {
	class Mat;
};

namespace frast {

	class FlatReader {
		public:
			FlatReader(const std::string& path, const EnvOptions& opts);

			int64_t determineTlbr(uint64_t tlbr[4]);

			cv::Mat getTile(uint64_t tile);

			void refreshMemMap();

			FlatEnvironment env;

		private:


			std::string openPath;
			EnvOptions openOpts;

			int determineDeepeseLevel();


	};



}
