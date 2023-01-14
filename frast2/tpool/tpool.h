#pragma once

#include <vector>
#include <deque>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <fmt/core.h>

namespace frast {

struct WorkerMeta {
	std::thread thread;
};

using Key = uint64_t;

//
// A Thread Pool implementation that allows a subclass to implement the virtual process() function,
// as well as virtual functions to construct/destruct per-worker user data.
//
// The design assumes that the threads can generate work from a `Key` which is just a uint64_t.
// For example, the tiff converter program will take an encoded tile id and the process()
// function should do whatever work and disk-IO it needs based on the key.
// In case some resource is not thread-safe, it should be part of the per-worker userData.
//

class ThreadPool {

	public:
		ThreadPool(int n);
		virtual ~ThreadPool();

		virtual void process(int workerId, const Key& key) =0;
		virtual void* createWorkerData(int workerId) =0;
		virtual void destroyWorkerData(int workerId, void* ptr) =0;

		inline void* getWorkerData(int workerId) { return workerDatas[workerId]; }

		void start();
		void stop();
		int  enqueue(const Key& k);

		void blockUntilFinishedPoll();

		inline std::mutex& getThreadPoolMutex() { return mtx; }
		inline int getThreadCount() { return workerMetas.size(); }

	private:

		std::deque<Key> queuedWork;

		// std::vector<std::thread> threads;
		// std::vector<std::condition_variable> cvs;
		// std::vector<std::mutex> mtxs;
		std::vector<WorkerMeta> workerMetas;
		std::vector<void*> workerDatas;

		virtual void workerLoop(int i);

		std::condition_variable cv;
		std::mutex mtx;

	protected:
		bool doStop_ = false;
};


}
