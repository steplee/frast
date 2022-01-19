#include "db.h"

#include <cstring>

#include <unordered_set>


/*
 *
 * App that merges two or more datasets into one larger one.
 * Supports multiple levels (datasets with overviews, as well as datasets with different base resolutions).
 *
 * NOTE: Ignore below comment: I have made the changes it suggests.
 * Note: With frastAddo requiring one level inputs, it must be run BEFORE frastMerge,
 *       which is not great.
 *       I think the way to fix this would be to modify frastAddo to create a new tile only if it does not yet exist,
 *       and remove the limitation of making input have one level.
 *       The '--clean' option would then only delete tiles who DO have four parents,
 *       only dropping levels with a resulting set of 0 tiles.
 *
 *
 * Notes:
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
 *			[It'd be better to do: For each tile: For each dataset: process())]
 *			For now I will just assume that won't happen often (it shouldn't, really).
 *
 */



int main(int argc, char** argv) {
	
	if (argc == 1 or argc == 2 or argc == 3) {
		printf(" - Usage:\n\n\tfrastMerge <outName> <inName1> <inName2> ...\n\n - You must provide at least two input datasets.\n");
		return 1;
	}

	std::string outDsetPath = std::string { argv[1] };

	std::vector<std::string> inDsetPaths;
	inDsetPaths.reserve(argc-1);
	for (int i=0; i<argc-2; i++)
		inDsetPaths.push_back(std::string { argv[i+2] });


	// Copy the first dataset, then open it.
	char cmd[1024];
	sprintf(cmd, "ls '%s'", inDsetPaths[0].c_str());
	int ret1 = system(cmd);
	if (ret1) {
		printf(" - Command \"%s\" failed, are you sure all input dsets exist?", cmd);
		return 1;
	}
	sprintf(cmd, "cp -r '%s' '%s'", inDsetPaths[0].c_str(), outDsetPath.c_str());
	printf(" - Running \"%s\"\n", cmd);
	int ret2 = system(cmd);
	if (ret1) {
		printf(" - Command \"%s\" failed, are you sure all input dsets exist, and parent out dir exists and is writable?", cmd);
		return 1;
	}
	printf(" - Done running cp\n", cmd);

	DatabaseOptions dopts;
	DatasetWritable outDset { outDsetPath, dopts };
	std::vector<std::unique_ptr<DatasetReader>> inDsets;
	inDsets.reserve(inDsetPaths.size());
	for (auto path : inDsetPaths) inDsets.push_back(std::make_unique<DatasetReader>(path));

	outDset.configure(1,8);
	
	// Determine min and max lvls
	int minLevel = 0, maxLevel = MAX_LVLS-1;
	while (1) {
		bool anyHaveLvl = false;
		for (auto &dset : inDsets) {
			if (dset->hasLevel(minLevel)) {
				anyHaveLvl = true;
				break;
			}
		}
		if (not anyHaveLvl)
			minLevel++;
		else break;
		if (minLevel >= MAX_LVLS) break;
	}
	while (1) {
		bool anyHaveLvl = false;
		for (auto &dset : inDsets) {
			if (dset->hasLevel(maxLevel)) {
				anyHaveLvl = true;
				break;
			}
		}
		if (not anyHaveLvl)
			maxLevel--;
		else break;
		if (maxLevel <= 0) break;
	}
	printf(" - Level span: %d -> %d\n", minLevel, maxLevel);
	if (maxLevel <= 0 or minLevel >= MAX_LVLS) {
		printf(" - invalid level span.\n");
		return 1;
	}

	// Begin incremental addition algo.
	// TODO: make multi-threaded.

	//for (int lvl=minLevel; lvl<=maxLevel; lvl++) {
	for (int lvl=maxLevel; lvl>=minLevel; lvl--) {
		// We could also use the Dataset::tileExists() function.
		std::unordered_set<uint64_t> seen;

		// Note: w_txn should be in inner loop, because on collisions we will access results
		// from the last dataset that have just been added.

		for (int dseti=0; dseti<inDsets.size(); dseti++) {
			auto& inDset = *inDsets[dseti];

			int tid = 0;

			MDB_txn* r_txn;
			if (inDset.beginTxn(&r_txn, true)) throw std::runtime_error("failed to begin txn");

			// The first dataset was copied, needn't check anything for it, be we do need to fill in 'seen'
			if (dseti == 0) {

				inDset.iterLevel(lvl, r_txn, [&seen, &outDset, &inDsets, dseti, tid](const BlockCoordinate& bc, MDB_val& val) {
					seen.insert(bc.c);
				});

			} else {

				MDB_txn* w_txn;
				if (outDset.beginTxn(&w_txn)) throw std::runtime_error("failed to begin txn");

				inDset.iterLevel(lvl, r_txn, [&seen, &outDset, &inDsets, dseti, tid](const BlockCoordinate& bc, MDB_val& val) {
						auto it = seen.find(bc.c);
						if (it == seen.end()) {
							// No collision happened: just add data without decode/encode
							seen.insert(bc.c);
							auto &wtile = outDset.blockingGetTileBufferForThread(tid);
							wtile.fillWith(bc, val);
							outDset.sendCommand(Command{Command::TileReady, wtile.bufferIdx});
						} else {
							// Collision happened: must decode both images, merge, encode, and add.
							printf(" - Collision on tile %luz %luy %lux [dset %d / %d]\n",
									bc.z(),bc.y(),bc.x(),dseti+1,inDsets.size());
	
							// ================================================
							// TODO: decode, Image::merge_keep, encode, write
							// ================================================
						}
				});

				if (outDset.endTxn(&w_txn)) throw std::runtime_error("failed to end txn");
			}

			if (inDset.endTxn(&r_txn)) throw std::runtime_error("failed to end txn");

		}
	}

	
	// Fin.

	return 0;
}
