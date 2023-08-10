#pragma once

#include "frast2/tpool/tpool.h"
#include "flat_env.h"
#include <atomic>


namespace frast {

	class FlatReader;

struct WriteCfg {
	int tileSize = 256;
};

struct ConvertConfig {
	std::vector<std::string> srcPaths;
	int baseLevel;
	bool addo = false;
	int channels=3;
	int addoInterp=1; // opencv value: https://docs.opencv.org/3.4/da/d54/group__imgproc__transform.html
	double tlbr[4]={0};
};

struct ProcessedData {
	static constexpr uint64_t INVALID_VALUE_LENGTH = ~(0lu);
	// NOTE: this pointer should have been allocated with malloc,
	// it will be freed by the writerThread.
	uint64_t key;
	void* value = nullptr;
	uint64_t valueLength = INVALID_VALUE_LENGTH;

	inline bool invalid() const { return valueLength == INVALID_VALUE_LENGTH; }

	inline bool operator<(const ProcessedData& o) const { return key < o.key; }
};

// Writer component that uses a single input GDAL file (probably a vrt)
class WriterMasterGdal : public ThreadPool {
	public:
		WriterMasterGdal(const std::string& outPath, const EnvOptions& opts, int threads=FRAST_WRITER_THREADS);
		virtual ~WriterMasterGdal();

		void start(const ConvertConfig& cfg);

		inline bool didWriterLoopExit() { return writerLoopExited.load(); }
		inline bool isTerrain() const { return env.isTerrain(); }

	public:
		virtual void process(int workerId, const Key& key) override;
		virtual void* createWorkerData(int workerId) override;
		virtual void destroyWorkerData(int workerId, void* ptr) override;

	private:
		FlatEnvironment env;
		ConvertConfig cfg;
		EnvOptions envOpts;

		std::vector<ProcessedData> processedData;
		std::mutex writerMtx;
		std::condition_variable writerCv;
		std::thread writerThread;
		void writerLoop();
		std::atomic_bool writerLoopExited = false;

		int lastNumEnqueued = 0;
	private:
		// A stateful generator called repeatedly in writerLoop()
		std::vector<uint64_t> yieldNextKeys();
		// void getNumTilesForLevel(uint64_t& outW, uint64_t& outH, int lvl);
		void handleProcessedData(std::vector<ProcessedData>& processedData);

		// Things that are private to writer_gdal.cc (the actual conversion code)
		void* masterData = nullptr;
		void* create_gdal_stuff(int workerId);
		void destroy_master_data();
		int curLevel=-1, curIndex=0;

		// This data is set on the *main thread* in start(), then kept constant for the remainder of the lifetime.
		// It could be used from children, which is ok.
		// (Although it's only needed for yieldNextKeys() in the writer thread)
		uint64_t levelTlbr[4];
		uint32_t levelTlbrStored=-1;
		void set_level_tlbr_from_main_thread(void* dset);
};

// Writer component that uses a single input GDAL file (probably a vrt)
class WriterMasterGdalMany : public ThreadPool {
	public:
		WriterMasterGdalMany(const std::string& outPath, const EnvOptions& opts, int threads=FRAST_WRITER_THREADS);
		virtual ~WriterMasterGdalMany();

		void start(const ConvertConfig& cfg);

		inline bool didWriterLoopExit() { return writerLoopExited.load(); }
		inline bool isTerrain() const { return env.isTerrain(); }

	public:
		virtual void process(int workerId, const Key& key) override;
		virtual void* createWorkerData(int workerId) override;
		virtual void destroyWorkerData(int workerId, void* ptr) override;

	private:
		FlatEnvironment env;
		ConvertConfig cfg;
		EnvOptions envOpts;

		std::vector<ProcessedData> processedData;
		std::mutex writerMtx;
		std::condition_variable writerCv;
		std::thread writerThread;
		void writerLoop();
		std::atomic_bool writerLoopExited = false;

		int lastNumEnqueued = 0;
	private:
		// A stateful generator called repeatedly in writerLoop()
		std::vector<uint64_t> yieldNextKeys();
		// void getNumTilesForLevel(uint64_t& outW, uint64_t& outH, int lvl);
		void handleProcessedData(std::vector<ProcessedData>& processedData);

		// Things that are private to writer_gdal.cc (the actual conversion code)
		void* masterData = nullptr;
		void* create_gdal_stuff(int workerId);
		void destroy_master_data();
		int curLevel=-1, curIndex=0;

		// This data is set on the *main thread* in start(), then kept constant for the remainder of the lifetime.
		// It could be used from children, which is ok.
		// (Although it's only needed for yieldNextKeys() in the writer thread)
		uint64_t levelTlbr[4];
		uint32_t levelTlbrStored=-1;
		void set_level_tlbr_from_main_thread(void* dset);
};

class WriterMasterAddo : public ThreadPool {
	public:
		WriterMasterAddo(const std::string& outPath, const EnvOptions& opts);
		virtual ~WriterMasterAddo();

		void start(const ConvertConfig& cfg);

		inline bool didWriterLoopExit() { return writerLoopExited.load(); }
		inline bool isTerrain() const { return env.isTerrain(); }

	public:
		virtual void process(int workerId, const Key& key) override;
		virtual void* createWorkerData(int workerId) override;
		virtual void destroyWorkerData(int workerId, void* ptr) override;

	private:
		FlatEnvironment env;
		ConvertConfig cfg;
		EnvOptions envOpts;

		std::vector<ProcessedData> processedData;
		std::mutex writerMtx;
		std::condition_variable writerCv;
		std::thread writerThread;
		void writerLoop();
		std::atomic_bool writerLoopExited = false;

		int lastNumEnqueued = 0;
	private:
		// A stateful generator called repeatedly in writerLoop()
		std::vector<uint64_t> yieldNextKeys();
		// void getNumTilesForLevel(uint64_t& outW, uint64_t& outH, int lvl);
		void handleProcessedData(std::vector<ProcessedData>& processedData);

		// Things that are private to writer_gdal.cc (the actual conversion code)
		void* masterData = nullptr;
		void* create_reader_stuff(int workerId);
		void destroy_master_data();
		int curLevel=-1, curIndex=0;
		std::string path_;

		// This data is set on the *main thread* in start(), then kept constant for the remainder of the lifetime.
		// It could be used from children, which is ok.
		// (Although it's only needed for yieldNextKeys() in the writer thread)
		uint32_t mainLvl;
		uint32_t mainTlbr[4];
		void set_main_tlbr_from_main_thread(FlatReader* reader);
};

}





/*
class WriterWorker {
	public:
		WriterWorker(WriterMaster* master);
	private:
		WriterMaster* master;
};
*/

