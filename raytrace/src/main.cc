




#include "trace.h"



int main() {

	Raytracer rt;

	Tile tile0;
	tile0.verts.allocateCpu(3);
	tile0.indices.allocateCpu(3);
	float S = .7f;
	tile0.verts(0).x = -.5 * S;
	tile0.verts(0).y = -.8 * S;
	tile0.verts(0).z =  .0;
	tile0.verts(1).x =  .5 * S;
	tile0.verts(1).y = -.8 * S;
	tile0.verts(1).z =  .0;
	tile0.verts(2).x =  .0;
	tile0.verts(2).y =  .8 * S;
	tile0.verts(2).z =  .0;
	tile0.indices(0) = 0;
	tile0.indices(1) = 1;
	tile0.indices(2) = 2;
	std::vector<Tile> tiles;
	tiles.push_back(std::move(tile0));

	rt.setGeometry(std::move(tiles));
	rt.render();

	return 0;
}
