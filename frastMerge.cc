#include "db.h"


/*
 *
 * App that merges two or more datasets into one larger one.
 * Supports multiple levels.
 * Note: With frastAddo requiring one level inputs, it must be run BEFORE frastMerge,
 *       which is not great.
 *       I think the way to fix this would be to modify frastAddo to create a new tile only if it does not yet exist,
 *       and remove the limitation of making input have one level.
 *       The '--clean' option would then only delete tiles who DO have four parents,
 *       only dropping levels with a resulting set of 0 tiles.
 *
 *
 *			- The number of channels must be equal.
 *			  TODO: Allow on-the-fly conversion.
 *
 *			- Tiles are NOT re-encoded, unless there are tile collisions
 *
 *			- Tile collisions will be resolved by preferring the left-most specified dataset,
 *			  unless the pixel in question is black, then the next available dataset's non-black pixel is taken.
 *
 *
 *
 *
 *	Algorithm:
 *			It'd be easiest to go from left-to-right dsets, incrementally adding tiles that don't exist or merging ones that collide.
 *			However, that would incur a lot of encode-decode-encode round trips for tiles with lots of collisions.
 *			That's bad for quality and performance.
 *			For now I will just assume that won't happen often (it shouldn't, really).
 *
 */



int main(int argc, char** argv) {
	
	if (argc == 1 or argc == 2 or argc == 3) {
		printf(" - Usage:\n\n\tfrastMerge <outName> <inName1> <inName2> ...\n\n - You must provide at least two input datasets.\n");
	}

	std::string outDsetPath = std::string { argv[1] };

	std::vector<std::string> inDsetPaths;
	inDsetPaths.reserve(argc-1);
	for (int i=0; i<argc-2; i++)
		inDsetPaths.push_back(std::string { argv[i+2] });

	DatabaseOptions dopts;
	DatasetWritable outDset { outDsetPath, dopts };

	// Copy the first dataset, then open it.
	// Begin incremental addition algo.
	// Fin.

	return 0;
}
