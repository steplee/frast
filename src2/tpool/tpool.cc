#include "tpool.h"

#include <unistd.h>
#include <cassert>

namespace frast {

ThreadPool::ThreadPool(int n) {
	workerDatas.resize(n);
	workerMetas.resize(n);

	// You should not construct a thread that calls a virtual method
	// from a derived class until after base is fully constructed.
	// So we cannot start the threads in the base constructor.
	//
	// The SAME logic goes for destructor.
	/*
	for (int i=0; i<n; i++) {
		WorkerMeta wm;
		wm.thread = std::move(std::thread(&ThreadPool::workerLoop, this, i));
		workerMetas[i] = std::move(wm);
	}
	*/
}

void ThreadPool::start() {
	for (int i=0; i<workerMetas.size(); i++) {
		WorkerMeta wm;
		wm.thread = std::move(std::thread(&ThreadPool::workerLoop, this, i));
		workerMetas[i] = std::move(wm);
	}
	// sleep(1);
}

void ThreadPool::stop() {
	mtx.lock();
	doStop_ = true;
	mtx.unlock();
	cv.notify_all();

	for (auto& meta : workerMetas)
		if (meta.thread.joinable()) {
			// meta.stop();
			meta.thread.join();
		}
}
ThreadPool::~ThreadPool() {
	assert(doStop_);
	for (auto& meta : workerMetas)
		assert(not meta.thread.joinable());
}

int ThreadPool::enqueue(const Key& k) {
	size_t n = 0;
	{
		std::lock_guard<std::mutex> lck(mtx);
		queuedWork.push_front(k);
		n = queuedWork.size();
	}

	// Reduce spurious wakeups in case we are pushing very fast.
	// Also helps to reduce context switching and use one thread for more work.
	// The 40>n>32 case should encourage at least 8 hw threads to be active
	// if (n == 1 or n == 8 or (n >= 32 and n < 40))
	// if (n == 1 or (n >= 32 and n < 40))
	if (n < 8 or (n >= 32 and n < 40))
	// if (n == 1)
		cv.notify_one();
	return n;
}

void ThreadPool::workerLoop(int I) {
	// auto &meta = workerMetas[I];

	workerDatas[I] = createWorkerData(I);
	bool haveStop = false;

	int nprocessed = 0;
	int wakeups    = 0;
	int spurious   = 0;

	while (not haveStop) {

		// Sleep until some work is available.
		std::unique_lock<std::mutex> lck(mtx);
		cv.wait(lck, [&] { return doStop_ or queuedWork.size(); });

		// Interestingly: this unlock, followed immediately by a lock() on the first do-while iter below,
		//                actually helps performance (when process() is a no-op).
		//                Without this unlock() we get zero spurious wakeups.
		//                With it we get more, but we also get more iters/wakeup (by time we've woken, there is more work)
		//                This only works when process() is simple and enqueue() is done fast
		//
		// It really looks like more iters/wakeup is the key (again, at least for when process() is a no-op)
		//
		// lck.unlock(); int firstProc = 1;
		int firstProc = 0;
		int nproc = firstProc;

		bool haveKey = false;

		// Loop so that this thread keeps going if there is more work (without sleeping)
		// Helps with both (less spurious wakeups & less total time taken)
		do {

			if (nproc > 0) lck.lock();

			// if (nproc > firstProc) fmt::print(" - check {}th loop: {}\n", nproc, queuedWork.size());

			Key key;
			haveKey = queuedWork.size() > 0;
			haveStop = doStop_;
			if (haveKey) {
				key = queuedWork.back();
				queuedWork.pop_back();
			} else if (not haveStop) spurious += nproc==firstProc;
			lck.unlock();

			if (haveStop) {
				// This @avg is the average number of items processed per wakeup, discounting spurious wakeups.
				double avg = ((double)nprocessed) / (wakeups-spurious);
				fmt::print(" - worker {} stopping ({} proc {} wakes {} spurious, {:.3f}p/(w-s))\n", I, nprocessed, wakeups, spurious, avg);
				break;
			}

			if (haveKey) {
				process(I, key);

				nproc++;
				nprocessed++;
			}

			// Maybe switch tasks before re-inspecting q.
			// Makes sense when pushing fast.
			// if (haveKey) sched_yield();

		} while (haveKey);

		wakeups++;
	}

	destroyWorkerData(I, workerDatas[I]);
}

void ThreadPool::blockUntilFinishedPoll() {
	while (true) {
		mtx.lock();
		auto size = queuedWork.size();
		mtx.unlock();
		if (size > 0) {
			// fmt::print(" - [blockUntilFinished] remaining size {}\n", size);
			usleep(1'000);
		} else
			break;
	}
}


}
