#include "writer.h"
#include "reader.h"
#include <algorithm>

#include <fmt/core.h>
#include <fmt/color.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace frast {

WriterMasterAddo::WriterMasterAddo(const std::string& outPath, const EnvOptions& opts)
	: ThreadPool(4),
	  path_(outPath),
	  env(outPath, opts) {


}

void* WriterMasterAddo::create_reader_stuff(int workerId) {
	return new FlatReader{path_, {}};
}

void* WriterMasterAddo::createWorkerData(int workerId) {
	// return nullptr;
	return create_reader_stuff(workerId);
}
void WriterMasterAddo::destroyWorkerData(int workerId, void *ptr) {
	auto dset = static_cast<FlatReader*>(ptr);
	delete dset;	
}
void WriterMasterAddo::destroy_master_data() {
	auto dset = static_cast<FlatReader*>(masterData);
	delete dset;	
}


void WriterMasterAddo::start(const ConvertConfig& cfg_) {
	cfg = cfg_;

	assert(cfg.baseLevel >= 0 and cfg.baseLevel < 30);
	assert(cfg.srcPaths.size() > 0);


	curLevel = cfg.baseLevel;
	masterData = create_reader_stuff(-1);
	writerThread = std::thread(&WriterMasterAddo::writerLoop, this);

	ThreadPool::start();
}

WriterMasterAddo::~WriterMasterAddo() {
	// Note: we have a different mtx and cv for the writerThread, so this may be UB...
	// (but it appears to work)
	stop();
	writerCv.notify_all();
	if (writerThread.joinable()) writerThread.join();

	destroy_master_data();

}



void WriterMasterAddo::writerLoop() {
	bool haveMoreWork = true;


	// auto dset = static_cast<MyGdalDataset*>(masterData);
	// uint64_t lvlTlbr[4];
	// dset->getTlbrForLevel(lvlTlbr, curLevel);
	// uint64_t w = lvlTlbr[2] - lvlTlbr[0];
	// uint64_t h = lvlTlbr[3] - lvlTlbr[1];

	curLevel = cfg.baseLevel - 1;

	/*
	while (haveMoreWork and !doStop_) {

		// TODO: This appears to work: now work on actual business code.

		// sleep(1);

		// Gather next batch of keys from tiff...
		std::vector<uint64_t> currKeys = yieldNextKeys();
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

	usleep(55'000);

	while (curLevel >= 0 and !doStop_) {
		uint64_t n_level = 0;
		haveMoreWork = true;
		bool began = false;
		curIndex = 0;

		// When we create a new level, we have to re-mmmap the file.
		/*
		fmt::print(" - will refresh master data\n");
		if (masterData)
			static_cast<FlatReader*>(masterData)->refreshMemMap();
		getThreadPoolMutex().lock();
		for (int i=0; i<getThreadCount(); i++) {
			fmt::print(" - will refresh {}\n", i);
			if (getWorkerData(i))
				static_cast<FlatReader*>(getWorkerData(i))->refreshMemMap();
		}
		getThreadPoolMutex().unlock();
		*/

		while (haveMoreWork and !doStop_) {

			std::vector<uint64_t> currKeys = yieldNextKeys();
			lastNumEnqueued = currKeys.size();

			// Begin level, if there are tiles.
			if (n_level == 0 and lastNumEnqueued > 0) {
				fmt::print(fmt::fg(fmt::color::lime), " - Beginning level {}\n", curLevel);
				env.beginLevel(curLevel);
				began = true;
			}

			n_level += lastNumEnqueued;
			haveMoreWork = lastNumEnqueued > 0;


			// Enqueue them
			for (auto key : currKeys) enqueue(key);

			// Wait until workers complete. We need an exact match.
			if (lastNumEnqueued>0) {
				std::unique_lock<std::mutex> lck(writerMtx);
				writerCv.wait(lck, [&] { return doStop_ or processedData.size() == currKeys.size(); });
				fmt::print(fmt::fg(fmt::color::green), " - All {} items ready.\n", processedData.size());

				handleProcessedData(processedData);
			}

		}

		if (began) env.endLevel(false);

		if (n_level == 0) {
			if (!doStop_)
				fmt::print(" - Stopping on level {} (no more items)\n", curLevel);
			break;
		}

		curLevel--;
	}




	fmt::print(" - writerLoop exiting.\n");
	writerLoopExited = true;
}


void WriterMasterAddo::handleProcessedData(std::vector<ProcessedData>& processedData) {
	// Write all of that data.
	// TODO: do this on another thread than the one that queues data?
	std::sort(processedData.begin(), processedData.end());

	fmt::print(" - addo handle proc {}\n", processedData.size());

	for (auto& pd : processedData) {
		if (pd.value == nullptr) assert(pd.valueLength == ProcessedData::INVALID_VALUE_LENGTH);
		if (pd.valueLength == ProcessedData::INVALID_VALUE_LENGTH) assert(pd.value == nullptr);
		if (pd.value != nullptr) assert(pd.valueLength != ProcessedData::INVALID_VALUE_LENGTH);
		if (pd.valueLength == 0) assert(false && "0-length value not supported yet (but will be!)");

		if (pd.value != nullptr) {
			// fmt::print(" - write k {}, vl {}\n", pd.key, pd.valueLength);
			env.writeKeyValue(pd.key, pd.value, pd.valueLength);
		} //else fmt::print(" - no write k {}, vl {}\n", pd.key, pd.valueLength);
	}

	for (auto& pd : processedData) {
		if (pd.value)
			free(pd.value);
	}

	processedData.resize(0);
}


void WriterMasterAddo::getNumTilesForLevel(uint64_t& w, uint64_t& h, int lvl) {
	auto reader = static_cast<FlatReader*>(masterData);
	uint64_t lvlTlbr[4];
	uint64_t mainTlbr[4];
	auto mainLvl = reader->determineTlbr(mainTlbr);

	int64_t zoom = mainLvl - curLevel;
	assert(zoom > 0);
	assert(zoom < 30);

	lvlTlbr[0] = mainTlbr[0] / (1 << zoom);
	lvlTlbr[1] = mainTlbr[1] / (1 << zoom);
	lvlTlbr[2] = (mainTlbr[2] + ((1l<<zoom)-1l)) / (1 << zoom);
	lvlTlbr[3] = (mainTlbr[3] + ((1l<<zoom)-1l)) / (1 << zoom);

	w = lvlTlbr[2] - lvlTlbr[0];
	h = lvlTlbr[3] - lvlTlbr[1];
}

std::vector<uint64_t> WriterMasterAddo::yieldNextKeys() {
	std::vector<uint64_t> out;

	auto reader = static_cast<FlatReader*>(masterData);
	uint64_t lvlTlbr[4];
	uint64_t mainTlbr[4];
	auto mainLvl = reader->determineTlbr(mainTlbr); // FIXME: remove this call

	int64_t zoom = mainLvl - curLevel;
	assert(zoom > 0);
	assert(zoom < 30);

	lvlTlbr[0] = mainTlbr[0] / (1 << zoom);
	lvlTlbr[1] = mainTlbr[1] / (1 << zoom);
	// lvlTlbr[2] = (mainTlbr[2] + ((1l<<zoom)-1l)) / (1 << zoom);
	// lvlTlbr[3] = (mainTlbr[3] + ((1l<<zoom)-1l)) / (1 << zoom);
	lvlTlbr[2] = (mainTlbr[2]) / (1 << zoom);
	lvlTlbr[3] = (mainTlbr[3]) / (1 << zoom);
	fmt::print(" - level {}, tlbr {} {} -> {} {}\n", curLevel, lvlTlbr[0], lvlTlbr[1], lvlTlbr[2], lvlTlbr[3]);

	uint64_t w = lvlTlbr[2] - lvlTlbr[0];
	uint64_t h = lvlTlbr[3] - lvlTlbr[1];

	if (w == 0) {
		fmt::print(fmt::fg(fmt::color::magenta), " - [yieldNextKeys()] Reached beyond top row of lvl {}, yielding last {}.\n", curLevel, out.size());
		return out;
	}

	for (int n=0; n<256; n++) {

		uint64_t yy = curIndex / w;
		uint64_t xx = curIndex - (yy * w); // avoid %
		uint64_t y = (yy) + lvlTlbr[1];
		uint64_t x = (xx) + lvlTlbr[0];

		// if (y >= lvlTblr[1])
		if (y > lvlTlbr[3]) {
			fmt::print(fmt::fg(fmt::color::magenta), " - [yieldNextKeys()] Reached beyond top row of lvl {}, yielding last {}.\n", curLevel, out.size());
			break;
		}

		curIndex++;

		out.push_back(BlockCoordinate{(uint64_t)curLevel,y,x}.c);
	}
	return out;
}


void WriterMasterAddo::process(int workerId, const Key& key) {
	auto reader = static_cast<FlatReader*>(getWorkerData(workerId));

	BlockCoordinate above(key);
	BlockCoordinate ca(above.z()+1, (above.y()<<1)+0, (above.x()<<1)+0);
	BlockCoordinate cb(above.z()+1, (above.y()<<1)+1, (above.x()<<1)+0);
	BlockCoordinate cc(above.z()+1, (above.y()<<1)+0, (above.x()<<1)+1);
	BlockCoordinate cd(above.z()+1, (above.y()<<1)+1, (above.x()<<1)+1);

	cv::Mat imga = reader->getTile(ca.c);
	cv::Mat imgb = reader->getTile(cb.c);
	cv::Mat imgc = reader->getTile(cc.c);
	cv::Mat imgd = reader->getTile(cd.c);

	void* value = nullptr;
	uint64_t valueLength = 0;

	if (imga.empty() and imgb.empty() and imgc.empty() and imgd.empty()) {
		// All empty...
		value = nullptr;
		valueLength = ProcessedData::INVALID_VALUE_LENGTH;
		fmt::print(" - Empty tile for {} {} {}\n", above.z(), above.y(), above.x());
	} else {
		if (imga.empty() or imgb.empty() or imgc.empty() or imgd.empty()) {
			// Find the non empty image to use as a template -- the empty ones are like it, but filled zero
			cv::Mat* tmpl = nullptr;
			if (!imga.empty()) tmpl = &imga;
			else if (!imgb.empty()) tmpl = &imgb;
			else if (!imgc.empty()) tmpl = &imgc;
			else if (!imgd.empty()) tmpl = &imgd;

			if (imga.empty()) imga = cv::Mat::zeros(tmpl->rows, tmpl->cols, tmpl->type());
			if (imgb.empty()) imgb = cv::Mat::zeros(tmpl->rows, tmpl->cols, tmpl->type());
			if (imgc.empty()) imgc = cv::Mat::zeros(tmpl->rows, tmpl->cols, tmpl->type());
			if (imgd.empty()) imgd = cv::Mat::zeros(tmpl->rows, tmpl->cols, tmpl->type());
		}

		// Make joined image.
		int th = imga.rows;
		int tw = imga.cols;
		cv::Mat img(th*2, tw*2, imga.type());
		imga.copyTo(img(cv::Rect{0,0,tw,th}));
		imgb.copyTo(img(cv::Rect{0,th,tw,th}));
		imgc.copyTo(img(cv::Rect{tw,0,tw,th}));
		imgd.copyTo(img(cv::Rect{tw,th,tw,th}));

		// Half-scale it
		cv::resize(img,img, imga.size());


		// Encode.
		std::vector<uint8_t> buf;
		bool stat = cv::imencode(".jpg", img, buf);
		assert(stat);
		value = malloc(buf.size());
		memcpy(value, buf.data(), buf.size());
		valueLength = buf.size();
	}



	{
		std::unique_lock<std::mutex> lck(writerMtx);
		// processedData.push_back(ProcessedData{key, malloc(1), 1});
		processedData.push_back(ProcessedData{key, value, valueLength});

		// Don't wake master until workers push all data.
		if (processedData.size() == lastNumEnqueued)
			writerCv.notify_one();
	}

}

}


