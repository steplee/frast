#include "writer.h"
#include "reader.h"
#include "gdal_stuff.hpp"

#include "codec.h"

#include <Eigen/Geometry>
#include <algorithm>
#include <numeric>

#include <opencv2/core.hpp>

#include "blend.hpp"

// https://stackoverflow.com/questions/73748856/eigen3-with-libfmt-9-0
#include <fmt/core.h>
#include <fmt/ostream.h>
template <typename T>
struct fmt::formatter< T, std::enable_if_t< std::is_base_of<Eigen::DenseBase<T>, T>::value, char>> : ostream_formatter {};
template <typename T> struct fmt::formatter<Eigen::WithFormat<T>> : ostream_formatter {};

namespace {
	using namespace frast;

	bool image_is_black(const cv::Mat& img) {

		// optimization: check a few pixels first
		if (img.data[0] != 0 or img.data[5*img.step+5*img.channels()+1] != 0) {
			// std::cout << " known not black, no sum \n";
			return false;
		}

		auto r = cv::sum(img);
		// std::cout << " sum " << r << "\n";
		return r[0] == 0 and r[1] == 0 and r[2] == 0;
	}

	bool dset_intersects(MyGdalDataset* dset, Eigen::AlignedBox2f& box) {
		Eigen::AlignedBox2f dset_box{dset->tlbr_uwm.head<2>().cast<float>()};
		dset_box.extend(dset->tlbr_uwm.tail<2>().cast<float>());
		bool out = box.intersects(dset_box);
		if(0)
		fmt::print("check xsect ({} -> {}) vs ({} -> {}) => {}\n",
				dset_box.min().transpose(),
				dset_box.max().transpose(),
				box.min().transpose(),
				box.max().transpose(), out);
		return out;
	}


}

namespace frast {

void WriterMasterGdalMany::process(int workerId, const Key& key) {

	auto dsets = static_cast<std::vector<MyGdalDataset*>*>(getWorkerData(workerId));
	int tileSize=256;

	BlockCoordinate bc(key);
	double tlbr_wm[4] = {
		(static_cast<double>(  bc.x()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale,
		(static_cast<double>(  bc.y()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale,
		(static_cast<double>(1+bc.x()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale,
		(static_cast<double>(1+bc.y()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale
	};
	double tlbr_uwm[4] = {
		(static_cast<double>(  bc.x()) / (1lu<<bc.z()) * 2. - 1.),
		(static_cast<double>(  bc.y()) / (1lu<<bc.z()) * 2. - 1.),
		(static_cast<double>(1+bc.x()) / (1lu<<bc.z()) * 2. - 1.),
		(static_cast<double>(1+bc.y()) / (1lu<<bc.z()) * 2. - 1.)
	};

	Eigen::AlignedBox2f tlbrUwmBox {Vector2f{tlbr_uwm[0], tlbr_uwm[1]}};
	tlbrUwmBox.extend(Vector2f{tlbr_uwm[2], tlbr_uwm[3]});

	// fmt::print(" - worker {} proc tile {} {} {}\n", workerId, bc.z(), bc.y(), bc.x());
	std::vector<cv::Mat> imgs;
	for (auto dset : *dsets) {
		if (dset_intersects(dset, tlbrUwmBox)) {
			cv::Mat img = dset->getWmTile(tlbr_wm, tileSize, tileSize, 3);
			imgs.push_back(img);
		}
	}

	// NOTE: Choose blend function here
	//
	// cv::Mat img = blend_imgs_avg(imgs);
	cv::Mat img = blend_imgs_first(imgs);

	void* val = nullptr;
	auto valueLength = ProcessedData::INVALID_VALUE_LENGTH;

	if (!img.empty() and !image_is_black(img)) {
		// Encode.
		// `encodeValue` malloc()s. But our `processedData` queue is not lossy,
		// and the main thread will call free. So there is no leaks possible.
		Value v = encodeValue(img, isTerrain());
		val = v.value;
		valueLength = v.len;
	}

	{
		std::unique_lock<std::mutex> lck(writerMtx);
		// processedData.push_back(ProcessedData{key, malloc(1), 1});
		processedData.push_back(ProcessedData{key, val, valueLength});

		// Don't wake master until workers push all data.
		if (processedData.size() == lastNumEnqueued)
			writerCv.notify_one();
	}

}










WriterMasterGdalMany::WriterMasterGdalMany(const std::string& outPath, const EnvOptions& opts, int threads)
	: ThreadPool(threads),
	  env(outPath, opts), envOpts(opts) {

	if (envOpts.isTerrain) {
		throw std::runtime_error("the '*Many' version does not support terrain. You must build a vrt and input just that one file.");
	}

}

void WriterMasterGdalMany::start(const ConvertConfig& cfg_) {
	cfg = cfg_;

	if (envOpts.isTerrain) assert(cfg.channels == 1);

	assert(cfg.baseLevel >= 0 and cfg.baseLevel < 30);
	// Otherwise, you ought to use the not 'Many' version.
	assert(cfg.srcPaths.size() > 1);

	env.beginLevel(cfg.baseLevel);

	curLevel = cfg.baseLevel;
	masterData = create_gdal_stuff(-1);

	set_level_tlbr_from_main_thread(masterData);
	if (cfg_.tlbr[0] == 0 and cfg_.tlbr[2] == 0) {
	} else {
		uint32_t iwm[4];
		uint64_t old[4] = {levelTlbr[0], levelTlbr[1], levelTlbr[2], levelTlbr[3]};
		dwm_to_iwm(iwm, cfg_.tlbr, cfg.baseLevel);
		for (int i=0; i<4; i++) levelTlbr[i] = iwm[i];
		fmt::print(" - overriding tlbr from [{} {} => {} {}]\n", old[0],old[1], old[2], old[3]);
		fmt::print(" -                   to [{} {} => {} {}]\n", levelTlbr[0],levelTlbr[1], levelTlbr[2], levelTlbr[3]);
		double nnew = (levelTlbr[3]-levelTlbr[1])*(levelTlbr[2]-levelTlbr[0]);
		double nold = (old[3]-old[1])*(old[2]-old[0]);
		fmt::print(" -                   {:.4f}% kept tiles\n", 100. * (nnew/nold));
	}

	writerThread = std::thread(&WriterMasterGdalMany::writerLoop, this);

	ThreadPool::start();
}

WriterMasterGdalMany::~WriterMasterGdalMany() {
	// Note: we have a different mtx and cv for the writerThread, so this may be UB...
	// (but it appears to work)
	stop();
	writerCv.notify_all();
	if (writerThread.joinable()) writerThread.join();

	env.endLevel(true);

	destroy_master_data();

}

void WriterMasterGdalMany::writerLoop() {
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

void WriterMasterGdalMany::handleProcessedData(std::vector<ProcessedData>& processedData) {
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















void WriterMasterGdalMany::set_level_tlbr_from_main_thread(void* dsets) {
	levelTlbr[0] = 9999999999;
	levelTlbr[1] = 9999999999;
	levelTlbr[2] = 0;
	levelTlbr[3] = 0;
	for (MyGdalDataset* dset : *((std::vector<MyGdalDataset*>*)dsets)) {
		uint64_t tlbr_[4];
		dset->getTlbrForLevel(tlbr_, curLevel);
		for (int i=0; i<2; i++) levelTlbr[i] = std::min(levelTlbr[i], tlbr_[i]);
		for (int i=0; i<2; i++) levelTlbr[2+i] = std::max(levelTlbr[2+i], tlbr_[2+i]);
	}
	levelTlbrStored = curLevel;
}

void* WriterMasterGdalMany::createWorkerData(int workerId) {
	// return nullptr;
	return create_gdal_stuff(workerId);
}
void WriterMasterGdalMany::destroyWorkerData(int workerId, void *ptr) {
	auto dsets = static_cast<std::vector<MyGdalDataset*>*>(ptr);
	delete dsets;	
}

void WriterMasterGdalMany::destroy_master_data() {
	auto dsets = static_cast<std::vector<MyGdalDataset*>*>(masterData);
	delete dsets;	
}

void* WriterMasterGdalMany::create_gdal_stuff(int worker_id) {
	// return new MyGdalDataset("/data/naip/mocoNaip/whole.wm.tif", envOpts.isTerrain);
	std::vector<MyGdalDataset*>* out = new std::vector<MyGdalDataset*>();
	for (auto path : cfg.srcPaths) {
		out->push_back(new MyGdalDataset(path, envOpts.isTerrain));
	}
	return out;
}



/*
void WriterMasterGdalMany::getNumTilesForLevel(uint64_t& w, uint64_t& h, int lvl) {
	auto dset = static_cast<MyGdalDataset*>(masterData);
	uint64_t lvlTlbr[4];
	dset->getTlbrForLevel(lvlTlbr, curLevel);
	w = lvlTlbr[2] - lvlTlbr[0];
	h = lvlTlbr[3] - lvlTlbr[1];
}
*/

std::vector<uint64_t> WriterMasterGdalMany::yieldNextKeys() {
	std::vector<uint64_t> out;

	// auto dsets = static_cast<std::vector<MyGdalDataset*>*>(masterData);
	assert(levelTlbrStored == curLevel);
	uint64_t w = levelTlbr[2] - levelTlbr[0];
	uint64_t h = levelTlbr[3] - levelTlbr[1];

	for (int n=0; n<256; n++) {

		uint64_t yy = curIndex / w;
		uint64_t xx = curIndex - (yy * w); // avoid %
		uint64_t y = (yy) + levelTlbr[1];
		uint64_t x = (xx) + levelTlbr[0];

		if (xx == 0 and yy % 8 == 0) {
			fmt::print(" - yielding ({} {} / {} {}) ({:.1f}% done)\n",
					xx,yy, w,h, 100* ((double)curIndex) / ((w+1)*(h+1)));
		}

		// if (y >= lvlTblr[1])
		if (y > levelTlbr[3]) {
			fmt::print(fmt::fg(fmt::color::magenta), " - [yieldNextKeys()] Reached beyond top row, stopping.\n");
			break;
		}

		curIndex++;

		out.push_back(BlockCoordinate{(uint64_t)curLevel,y,x}.c);
	}
	return out;
}



}
