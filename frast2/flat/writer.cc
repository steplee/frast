#include "writer.h"
#include <algorithm>

#include <fmt/core.h>
#include <fmt/color.h>

namespace frast {

WriterMaster::WriterMaster(const std::string& outPath, const EnvOptions& opts)
	: ThreadPool(4),
	  env(outPath, opts), envOpts(opts) {


}

void WriterMaster::start(const ConvertConfig& cfg_) {
	cfg = cfg_;

	if (envOpts.isTerrain) assert(cfg.channels == 1);

	assert(cfg.baseLevel >= 0 and cfg.baseLevel < 30);
	assert(cfg.srcPaths.size() > 0);
	assert(cfg.srcPaths.size() == 1); // for now...

	env.beginLevel(cfg.baseLevel);

	curLevel = cfg.baseLevel;
	masterData = create_gdal_stuff(-1);
	writerThread = std::thread(&WriterMaster::writerLoop, this);

	ThreadPool::start();
}

WriterMaster::~WriterMaster() {
	// Note: we have a different mtx and cv for the writerThread, so this may be UB...
	// (but it appears to work)
	stop();
	writerCv.notify_all();
	if (writerThread.joinable()) writerThread.join();

	env.endLevel(true);

	destroy_master_data();

}



void WriterMaster::writerLoop() {
	bool haveMoreWork = true;


	// auto dset = static_cast<MyGdalDataset*>(masterData);
	// uint64_t lvlTlbr[4];
	// dset->getTlbrForLevel(lvlTlbr, curLevel);
	// uint64_t w = lvlTlbr[2] - lvlTlbr[0];
	// uint64_t h = lvlTlbr[3] - lvlTlbr[1];



	while (haveMoreWork and !doStop_) {

		// TODO: This appears to work: now work on actual business code.

		// sleep(1);

		// Gather next batch of keys from tiff...
		std::vector<uint64_t> currKeys = yieldNextKeys();
		lastNumEnqueued = currKeys.size();
		haveMoreWork = lastNumEnqueued > 0;
		// fmt::print(fmt::fg(fmt::color::green), " - Enqueing {} items ready.\n", lastNumEnqueued);

		// Enqueue them
		for (auto key : currKeys) enqueue(key);

		// Wait until workers complete. We need an exact match.
		{
			std::unique_lock<std::mutex> lck(writerMtx);
			writerCv.wait(lck, [&] { return doStop_ or processedData.size() == currKeys.size(); });
			// fmt::print(fmt::fg(fmt::color::green), " - All {} items ready.\n", processedData.size());

			handleProcessedData(processedData);
		}
	}

	/*
	if (cfg.addo) {

		uint64_t w=1,h=1;
		int level = args.baseLevel;

		while (level > 0 and (w > 0 or h > 0)) {

			level--;
			getNumTilesForLevel(w,h,level);

			haveMoreWork = true;
			while (haveMoreWork and !doStop_) {

				std::vector<uint64_t> currKeys = yieldNextKeysAddo();
				lastNumEnqueued = currKeys.size();
				haveMoreWork = lastNumEnqueued > 0;
				fmt::print(fmt::fg(fmt::color::green), " - Enqueing {} items ready.\n", lastNumEnqueued);

				// Enqueue them
				for (auto key : currKeys) enqueue(key);

				// Wait until workers complete. We need an exact match.
				{
					std::unique_lock<std::mutex> lck(writerMtx);
					writerCv.wait(lck, [&] { return doStop_ or processedData.size() == currKeys.size(); });
					fmt::print(fmt::fg(fmt::color::green), " - All {} items ready.\n", processedData.size());

					handleProcessedData(processedData);
				}
			}

		}
	}
	*/


	fmt::print(" - writerLoop exiting.\n");
	writerLoopExited = true;
}


void WriterMaster::handleProcessedData(std::vector<ProcessedData>& processedData) {
	// Write all of that data.
	// TODO: do this on another thread than the one that queues data?
	std::sort(processedData.begin(), processedData.end());

	for (auto& pd : processedData) {
		if (pd.value == nullptr) assert(pd.valueLength == ProcessedData::INVALID_VALUE_LENGTH);
		if (pd.valueLength == ProcessedData::INVALID_VALUE_LENGTH) assert(pd.value == nullptr);
		if (pd.value != nullptr) assert(pd.valueLength != ProcessedData::INVALID_VALUE_LENGTH);
		if (pd.valueLength == 0) assert(false && "0-length value not supported yet (but will be!)");

		if (pd.value != nullptr) {
			// fmt::print(" - write k {}, vl {}\n", pd.key, pd.valueLength);
			env.writeKeyValue(pd.key, pd.value, pd.valueLength);
		}
	}

	for (auto& pd : processedData) {
		if (pd.value)
			free(pd.value);
	}

	processedData.resize(0);
}

}

