#pragma once

#include "tpool/tpool.h"
#include "flat_env.h"
#include <atomic>


namespace frast {

// class MyGdalDataset;

struct WriteCfg {
	int tileSize = 256;
};

struct ConvertConfig {
	std::vector<std::string> srcPaths;
	int baseLevel;
	bool addo = false;
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

class WriterMaster : public ThreadPool {
	public:
		WriterMaster(const std::string& outPath, const EnvOptions& opts);
		virtual ~WriterMaster();

		// inline const WriteCfg& getCfg() const { return cfg; }

		void start(const ConvertConfig& cfg);

		inline bool didWriterLoopExit() { return writerLoopExited.load(); }

	public:
		virtual void process(int workerId, const Key& key) override;
		virtual void* createWorkerData(int workerId) override;
		virtual void destroyWorkerData(int workerId, void* ptr) override;

	private:
		FlatEnvironment env;
		// WriteCfg cfg;
		ConvertConfig cfg;

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
		void getNumTilesForLevel(uint64_t& outW, uint64_t& outH, int lvl);
		void handleProcessedData(std::vector<ProcessedData>& processedData);

		// Things that are private to writer_gdal.cc (the actual conversion code)
		void* masterData = nullptr;
		void* create_gdal_stuff(int workerId);
		void destroy_master_data();
		int curLevel=-1, curIndex=0;
};


class WriterMasterAddo : public ThreadPool {
	public:
		WriterMasterAddo(const std::string& outPath, const EnvOptions& opts);
		virtual ~WriterMasterAddo();

		void start(const ConvertConfig& cfg);

		inline bool didWriterLoopExit() { return writerLoopExited.load(); }

	public:
		virtual void process(int workerId, const Key& key) override;
		virtual void* createWorkerData(int workerId) override;
		virtual void destroyWorkerData(int workerId, void* ptr) override;

	private:
		FlatEnvironment env;
		ConvertConfig cfg;

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
		void getNumTilesForLevel(uint64_t& outW, uint64_t& outH, int lvl);
		void handleProcessedData(std::vector<ProcessedData>& processedData);

		// Things that are private to writer_gdal.cc (the actual conversion code)
		void* masterData = nullptr;
		void* create_reader_stuff(int workerId);
		void destroy_master_data();
		int curLevel=-1, curIndex=0;
		std::string path_;
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

