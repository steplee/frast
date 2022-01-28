#include <vector>
#include <cmath>
#include "db.h"

namespace {


}


int main(int argc, char** argv) {

	if (argc <= 1) {
		printf(" - Must specify a dataset.\n");
		return 1;
	}

	std::string dsetPath { argv[1] };
	Dataset dset { dsetPath };
	double circumBox[4] = {9e19,9e19, -9e19,-9e19};

	printf(" === Dataset '%s' ===\n", dsetPath.c_str());

	std::vector<int> lvls;
	for (int i=0; i<MAX_LVLS; i++)
		if (dset.hasLevel(i)) lvls.push_back(i);

	printf(" - Have %zu levels:\n\t", lvls.size());
	for (int i=lvls.size()-1; i>=0; i--) {
			printf("%2d", lvls[i]);
			if (i != lvls.size()-1 and i % 8 == 0 and i != 0) printf("\n\t");
			else if (i != 0) printf(", ");
	}
	printf("\n");

	uint64_t nTotal = 0;
	for (int i=lvls.size()-1; i>=0; i--) {
		int lvl = lvls[i];
		uint64_t tlbr[4];
		dset.determineLevelAABB(tlbr, lvl);
		uint64_t w = tlbr[2] - tlbr[0];
		uint64_t h = tlbr[3] - tlbr[1];
		uint64_t n = w*h;
		nTotal += n;
		printf(" - Lvl %2d: %6lu %6lu -> %6lu %6lu (%4luw %4luh, %7lu total)\n",
				lvl, tlbr[0],tlbr[1], tlbr[2],tlbr[3], w,h,n);

		double s = 2. * WebMercatorMapScale / (1 << lvl);
		double x1 = tlbr[0] * s - WebMercatorMapScale;
		double x2 = tlbr[2] * s - WebMercatorMapScale;
		double y1 = tlbr[1] * s - WebMercatorMapScale;
		double y2 = tlbr[3] * s - WebMercatorMapScale;

		// Taking all levels actually results in a much too large box, due to partially blank overviews.
		if (i == lvls.size() - 1) {
			if (x1 < circumBox[0]) circumBox[0] = x1;
			if (x2 > circumBox[2]) circumBox[2] = x2;
			if (y1 < circumBox[1]) circumBox[1] = y1;
			if (y2 > circumBox[3]) circumBox[3] = y2;
		}
	}

	double wm_w = circumBox[2] - circumBox[0];
	double wm_h = circumBox[3] - circumBox[1];
	double scaleFactorInv = 1. / std::cosh((circumBox[1] + circumBox[3]) / (2. * WebMercatorMapScale) * M_PI);
	printf(" - Total tiles: %lu\n", nTotal);
	printf(" - Deepest level enclosing box: %lf %lf %lf %lf\n", circumBox[0], circumBox[1], circumBox[2], circumBox[3]);
	printf("                                %.2lf %.2lf [WebMercator scaled-meters]\n", wm_w, wm_h);
	printf("                                %.2lf [²km wm]\n", (wm_w * wm_h) / 1e6);
	printf("                               ~%.2lf [²km actual]\n", (wm_w * wm_h * scaleFactorInv * scaleFactorInv) / 1e6);

	return 0;
}
