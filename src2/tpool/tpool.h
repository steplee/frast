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
	// std::condition_variable cv;
	// std::mutex mtx;


	/*inline void stop() {
		mtx.lock();
		doStop_ = true;
		mtx.unlock();
		cv.notify_one();
	}*/
};

using Key = uint64_t;

class ThreadPool {

	public:
		ThreadPool(int n);
		virtual ~ThreadPool();

		virtual void process(int workerId, const Key& key) =0;
		virtual void* createUserData(int workerId) =0;
		virtual void destroyUserData(int workerId) =0;

		inline void* getUserData(int workerId) { return userDatas[workerId]; }

		void start();
		void stop();
		int  enqueue(const Key& k);

		void blockUntilFinished();

	private:

		std::deque<Key> queuedWork;

		// std::vector<std::thread> threads;
		// std::vector<std::condition_variable> cvs;
		// std::vector<std::mutex> mtxs;
		std::vector<WorkerMeta> workerMetas;
		std::vector<void*> userDatas;

		virtual void workerLoop(int i);
		bool doStop_ = false;

		std::condition_variable cv;
		std::mutex mtx;
};


}
