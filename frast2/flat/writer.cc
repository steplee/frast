#include "writer.h"
#include <algorithm>

#include <fmt/core.h>
#include <fmt/color.h>

namespace frast {

WriterMasterGdal::WriterMasterGdal(const std::string& outPath, const EnvOptions& opts, int threads)
	: ThreadPool(threads),
	  env(outPath, opts), envOpts(opts) {


}

void WriterMasterGdal::start(const ConvertConfig& cfg_) {
	cfg = cfg_;

	if (envOpts.isTerrain) assert(cfg.channels == 1);

	assert(cfg.baseLevel >= 0 and cfg.baseLevel < 30);
	assert(cfg.srcPaths.size() > 0);
	assert(cfg.srcPaths.size() == 1); // for now...

	env.beginLevel(cfg.baseLevel);

	curLevel = cfg.baseLevel;
	masterData = create_gdal_stuff(-1);
	set_level_tlbr_from_main_thread(masterData);
	writerThread = std::thread(&WriterMasterGdal::writerLoop, this);

	ThreadPool::start();
}

WriterMasterGdal::~WriterMasterGdal() {
	// Note: we have a different mtx and cv for the writerThread, so this may be UB...
	// (but it appears to work)
	stop();
	writerCv.notify_all();
	if (writerThread.joinable()) writerThread.join();

	env.endLevel(true);

	destroy_master_data();

}



void WriterMasterGdal::writerLoop() {
	bool haveMoreWork = true;

	while (haveMoreWork and !doStop_) {

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

	fmt::print(" - writerLoop exiting.\n");
	writerLoopExited = true;
}


void WriterMasterGdal::handleProcessedData(std::vector<ProcessedData>& processedData) {
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

