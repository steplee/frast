
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>
#include <unistd.h>
#include <atomic>
#include <unordered_set>

#include "tpool.h"

using namespace frast;

class Test_ThreadPool : public ThreadPool {

	public:

		// static constexpr int THREADS = 16;
		static constexpr int THREADS = 4;
		// static constexpr int THREADS = 2;
		// static constexpr int THREADS = 1;
		// static constexpr int THREADS = 3;

		inline Test_ThreadPool() : ThreadPool(THREADS) {
			for (int i=0; i<THREADS; i++) cnts[i] = 0;
		};
		inline virtual ~Test_ThreadPool() {
			fmt::print(" - process() Histogram:\n");
			int total = 0;
			for (int i=0; i<THREADS; i++) {
				fmt::print(" - {:>2d}| {:>8d}\n", i, cnts[i]);
				total += cnts[i];
			}
			fmt::print(" -> total {}\n", total);
		}

		std::mutex mtx;
		std::unordered_set<int> seen;
		int n_duplicate = 0;

		// std::atomic_int cnts[THREADS];
		int cnts[THREADS];

		inline virtual void process(int workerId, const Key& key) override {
			// fmt::print(" - process {} on thread {}\n", key, workerId);
			cnts[workerId] += 1;

			mtx.lock();
			n_duplicate += seen.find(key) != seen.end();
			seen.insert(key);
			mtx.unlock();
		}
		inline virtual void* createUserData(int workerId) override {
			fmt::print(" - create user data {}\n", workerId);
			return nullptr;
		}
		inline virtual void destroyUserData(int workerId) override {
			fmt::print(" - destroy user data {}\n", workerId);
		}

};


TEST_CASE( "tpool", "[tpool]" ) {

	// Test_ThreadPool tpool;
	Test_ThreadPool* tpool = new Test_ThreadPool();
	tpool->start();

	fmt::print(" - begin enqueuing\n");
	for (int i=0; i<(1<<22); i++) {
		int n = tpool->enqueue(i);
		if (i % (1<<18) == 0)
			fmt::print(" - ({:>8d}/{:>8d}) q size {}\n", i, (1<<22), n);

		if (n>1<<12) usleep(100);
	}
	fmt::print(" - done  enqueuing\n");

	tpool->blockUntilFinished();

	REQUIRE(tpool->n_duplicate == 0);

	tpool->stop();
	delete tpool;


}
