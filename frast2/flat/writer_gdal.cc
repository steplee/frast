#include "writer.h"
#include "gdal_stuff.hpp"

#include "codec.h"

namespace {
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
}

namespace frast {

void WriterMasterGdal::set_level_tlbr_from_main_thread(void* dset) {
	((MyGdalDataset*)dset)->getTlbrForLevel(levelTlbr, curLevel);
	levelTlbrStored = curLevel;
}

void* WriterMasterGdal::createWorkerData(int workerId) {
	// return nullptr;
	return create_gdal_stuff(workerId);
}
void WriterMasterGdal::destroyWorkerData(int workerId, void *ptr) {
	auto dset = static_cast<MyGdalDataset*>(ptr);
	delete dset;	
}

void WriterMasterGdal::destroy_master_data() {
	auto dset = static_cast<MyGdalDataset*>(masterData);
	delete dset;	
}

void* WriterMasterGdal::create_gdal_stuff(int worker_id) {
	// return new MyGdalDataset("/data/naip/mocoNaip/whole.wm.tif", envOpts.isTerrain);
	return new MyGdalDataset(cfg.srcPaths[0], envOpts.isTerrain);
}


void WriterMasterGdal::process(int workerId, const Key& key) {
	// fmt::print(" - worker {} proc key {}\n", workerId, key);

	// auto dset = static_cast<MyGdalDataset*>(gdalDset);
	auto dset = static_cast<MyGdalDataset*>(getWorkerData(workerId));
	int tileSize=256;

	BlockCoordinate bc(key);
	double tlbr[4] = {
		(static_cast<double>(  bc.x()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale,
		(static_cast<double>(  bc.y()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale,
		(static_cast<double>(1+bc.x()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale,
		(static_cast<double>(1+bc.y()) / (1lu<<bc.z()) * 2. - 1.) * WebMercatorMapScale
	};

	// fmt::print(" - worker {} proc tile {} {} {}\n", workerId, bc.z(), bc.y(), bc.x());
	cv::Mat img = dset->getWmTile(tlbr, tileSize, tileSize, 3);

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

void WriterMasterGdal::getNumTilesForLevel(uint64_t& w, uint64_t& h, int lvl) {
	auto dset = static_cast<MyGdalDataset*>(masterData);
	uint64_t lvlTlbr[4];
	dset->getTlbrForLevel(lvlTlbr, curLevel);
	w = lvlTlbr[2] - lvlTlbr[0];
	h = lvlTlbr[3] - lvlTlbr[1];
}

std::vector<uint64_t> WriterMasterGdal::yieldNextKeys() {
	std::vector<uint64_t> out;

	auto dset = static_cast<MyGdalDataset*>(masterData);
	/*
	if (levelTlbrStored != curLevel) {
		dset->getTlbrForLevel(levelTlbr, curLevel);
		levelTlbrStored = curLevel;
	}
	*/
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
