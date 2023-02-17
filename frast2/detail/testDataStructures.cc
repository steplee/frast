#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>

#include "pqueue.hpp"

#include "env.h"
#include "bptree.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

using namespace frast;


TEST_CASE( "PriorityQueueTest1", "[pqueue]" ) {
	PriorityQueue<int> pq;

	std::vector<int> nums { 5,3,0,1,2,2,2,2,-2,8,3,3,8,3 };
	for (auto i : nums) pq.add(i);

	auto sortedNums = nums;
	std::sort(sortedNums.begin(), sortedNums.end());

	// PriorityQueue<int> pqCopy { pq };
	// for (int iter=0; iter<nums.size(); iter++) fmt::print(" - popping {}\n", pqCopy.pop());

	for (int iter=0; iter<nums.size(); iter++) {
		REQUIRE(pq.size() == nums.size()-iter);
		REQUIRE(pq.pop() == sortedNums[sortedNums.size()-iter-1]);
	}
}

TEST_CASE( "EnvironmentTest1", "[environment]" ) {

	EnvOptions opts;
	opts.anon = true;
	opts.mapSize = 4096*32;
	PagedEnvironment env("", opts);

	auto sum_pages = [&env]() {
		int nalloc = 0;
		for (int i=0; i<env.npages(); i++) {
			// fmt::print(" - o {}\n", env.occ(i));
			nalloc += env.occ(i);
		}
		return nalloc;
	};


	// Test depth-one allocations.
	for (int i=0; i<4; i++)
	{
		void* a = env.allocatePages(1);
		REQUIRE(sum_pages() == 1);

		env.freePages(a, 1);
		REQUIRE(sum_pages() == 0);
	}

	// Test depth-two allocations.
	void* aa = env.allocatePages(4);
	REQUIRE(sum_pages() == 4);
	for (int i=1; i<5; i++)
	{
		void* a = env.allocatePages(i);
		REQUIRE(sum_pages() == 4+i);

		env.freePages(a, i);
		REQUIRE(sum_pages() == 4);
	}
	env.freePages(aa, 4);
	REQUIRE(sum_pages() == 0);

	// Test a failure case.
	{
		REQUIRE_THROWS( env.allocatePages(38) );
		REQUIRE(sum_pages() == 0);
	}

	// Test a mixed success/failure case.
	{
		void* a = env.allocatePages(14);
		REQUIRE(sum_pages() == 14);
		void* b = env.allocatePages(10);
		REQUIRE(sum_pages() == 24);
		REQUIRE_THROWS( env.allocatePages(10) );
		env.freePages(a, 14);
		REQUIRE(sum_pages() == 10);
		env.freePages(b, 10);
		REQUIRE(sum_pages() == 0);
	}

}

struct MyValue {
	int64_t v;

	inline static constexpr int size() { return sizeof(decltype(v)); }
	inline void* data() { return &v; }
	inline const void* data() const { return &v; }

	friend std::ostream& operator<<(std::ostream& os, const MyValue& dt);
};
std::ostream& operator<<(std::ostream& os, const MyValue& dt) {
	os << dt.v;
	return os;
}
template <> struct fmt::formatter<MyValue> : ostream_formatter {};

TEST_CASE( "BPTreeGraphViz", "[bptree]" ) {

	// BPTree<uint64_t,int>::InnerNode nd;
	// nd.id;
	

	EnvOptions opts;
	opts.anon = true;
	opts.mapSize = (1<<30) / 4; // 256 MB
	TheEnvironment env("", opts);

	// using MyBPTree = BPTree<uint64_t, MyValue>;
	using MyBPTree = BPTree<int64_t, MyValue,4>;
	void* rootBuffer = env.allocateBytes(sizeof(MyBPTree::RootNode));
	MyBPTree::prepareEmptyRoot(rootBuffer);
	MyBPTree tree(rootBuffer, env);
	REQUIRE(tree.size() == 0);

	// for (int i=2; i<7; i++) tree.insert(i, MyValue{i} );
	// for (int i=2; i<8; i++) tree.insert(9-i, MyValue{9-i} );
	// for (int i=2; i<11; i++) tree.insert(i, MyValue{i} );
	// for (int i=1; i<22; i++) tree.insert(i, MyValue{i} );

	// Generate unique random numbers
	constexpr int N = 42;
	// constexpr int N = 200;
	std::unordered_set<int> set;
	for (int i=1; set.size()<N; i++) {
		int kv = 2000+std::sin(i*1377.3973)*2000;
		set.insert(kv);
	}

	int i=0;
	for (auto kv : set) {
		tree.insert(kv, MyValue{kv} );
		REQUIRE(tree.size() == ++i);
	}
	REQUIRE(tree.size() == set.size());

	fmt::print(fmt::fg(fmt::color::lime), "\n - see ./tree.dot\n");
	auto dot = tree.dumpGraphviz();
	// fmt::print("{}\n", dot);
	std::ofstream ifs("tree.dot");
	ifs << dot;
}

TEST_CASE( "BPTreeSubTreeBounds", "[bptree]" ) {
	// Make sure that the condtions
	//       childKey[i  ] <  k[internalKey[i]]
	//       childKey[i+1] >= k[internalKey[i]]
	//
	// are met for all nodes.

	EnvOptions opts;
	opts.anon = true;
	opts.mapSize = (1<<30) / 4; // 256 MB
	TheEnvironment env("", opts);

	using MyBPTree = BPTree<int64_t, MyValue,4>;
	void* rootBuffer = env.allocateBytes(sizeof(MyBPTree::RootNode));
	MyBPTree::prepareEmptyRoot(rootBuffer);
	MyBPTree tree(rootBuffer, env);

	// for (int i=1; i<22; i++) tree.insert(i, MyValue{i} );
	for (int i=1; i<22; i++) {
		int kv = 600+std::sin(i*377.3)*500;
		tree.insert(kv, MyValue{kv} );
	}

	struct Item {
		MyBPTree::InnerNode* node;
		int64_t lowerEq, upper;
	};

	auto root = tree.root;
	std::stack<Item> st;

	st.push(Item{root, -9999999,9999999});

	while (not st.empty()) {
		auto item = st.top();
		st.pop();
		auto curr = item.node;
		int n = curr->nresident;

		if (curr->isBoundary) {

			// Check leaf keys, no recursion
			for (int i=0; i<n; i++) {
				// fmt::print(" - Check :: {} < {} <= {}\n", item.lowerEq, curr->bd.key[i], item.upper);
				REQUIRE(curr->bd.key[i] >= item.lowerEq);
				REQUIRE(curr->bd.key[i] < item.upper);
			}

		} else {

			// First check the keys of this level
			for (int i=0; i<n-1; i++) {
				// fmt::print(" - Check :: {} < {} <= {}\n", item.lowerEq, curr->id.key[i], item.upper);
				REQUIRE(curr->id.key[i] >= item.lowerEq);
				REQUIRE(curr->id.key[i] < item.upper);
			}

			// Recurse
			for (int i=0; i<n; i++) {
				if (i == 0)
					st.push({curr->id.children[i], item.lowerEq, curr->id.key[0]});
				else if (i == n-1)
					st.push({curr->id.children[i], curr->id.key[n-2], item.upper});
				else
					st.push({curr->id.children[i], curr->id.key[i-1], curr->id.key[i]});
			}

		}
	}
		
}

TEST_CASE( "BPTreeDuplicates", "[bptree]" ) {

	EnvOptions opts;
	opts.anon = true;
	opts.mapSize = (1<<30) / 4; // 256 MB
	TheEnvironment env("", opts);

	using MyBPTree = BPTree<int64_t, MyValue,4>;
	void* rootBuffer = env.allocateBytes(sizeof(MyBPTree::RootNode));
	MyBPTree::prepareEmptyRoot(rootBuffer);
	MyBPTree tree(rootBuffer, env);

	std::vector<int64_t> items {
		1, 2, 3, 4, 1, 1, 1, 2, 3, 4, 4, 4, 1, 2, 3, 3, 2
		// 1, 1
	};
	int N = items.size();
	for (auto x : items) tree.insert(x, MyValue{x} );


	fmt::print(fmt::fg(fmt::color::lime), "\n - see ./tree.dot\n");
	auto dot = tree.dumpGraphviz();
	// fmt::print("{}\n", dot);
	std::ofstream ifs("tree.dot");
	ifs << dot;

	REQUIRE(tree.size() == 4);
}
