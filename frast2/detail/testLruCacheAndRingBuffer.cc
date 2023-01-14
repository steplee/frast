#include <catch2/catch_test_macros.hpp>
// #include <gtest/gtest.h>
#include <unistd.h>
#include <algorithm>

#include "data_structures.hpp"

/* ===================================================
 *
 *
 *                  RingBuffer
 *
 *
 * =================================================== */

TEST_CASE( "Basic", "[RingBuffer]" ) {
	constexpr int cap = 16;

	RingBuffer<int32_t> rb(cap);
	REQUIRE(rb.empty());

	// Push until full, then pop em off
	for (int i = 0; i < cap; i++) REQUIRE(not rb.push_back(i));

	// We are now full. Trying to push again will fail
	REQUIRE(rb.isFull());
	REQUIRE(rb.size() == cap);
	REQUIRE(rb.push_back(1337));
	REQUIRE(rb.isFull());
	REQUIRE(rb.size() == cap);

	for (int i = 0; i < cap; i++) {
		int value;
		rb.pop_front(value);
		REQUIRE(value == i);
	}

	// We should now be empty
	REQUIRE(rb.empty());

	// Now repeat the entire process
	for (int i = 0; i < cap; i++) rb.push_back(i);
	REQUIRE(rb.isFull());
	for (int i = 0; i < cap; i++) {
		int value;
		REQUIRE(not rb.pop_front(value));
		REQUIRE(value == i);
	}
	REQUIRE(rb.empty());
}

TEST_CASE( "SmallAndLarge", "[RingBuffer]" ) {
	{
		constexpr int		cap = 2;
		RingBuffer<int32_t> rb(2);
		REQUIRE(not rb.push_back(0));
		REQUIRE(not rb.push_back(1));
		REQUIRE(rb.push_back(2));
		int32_t val;
		REQUIRE(not rb.pop_front(val));
		REQUIRE(not rb.pop_front(val));
		REQUIRE(rb.pop_front(val));
		REQUIRE(rb.empty());
	}

	{
		constexpr int		cap = 1 << 20;
		RingBuffer<int32_t> rb(cap);
		// Do three rounds of filling/emptying
		for (int i = 0; i < 3; i++) {
			REQUIRE(rb.empty());
			for (int i = 0; i < cap; i++) REQUIRE(not rb.push_back(i));
			REQUIRE(rb.push_back(0));  // fails
			REQUIRE(rb.isFull());
			int32_t val;
			for (int i = 0; i < cap; i++) REQUIRE(not rb.pop_front(val));
			REQUIRE(rb.pop_front(val));	 // fails
			REQUIRE(rb.empty());
		}
	}
}

/* ===================================================
 *
 *
 *                  LruCache
 *
 *
 * =================================================== */

// Test adding and retrieving
TEST_CASE( "SetAndGet", "[LruCache]" ) {
	constexpr int			   cap = 16;
	LruCache<int32_t, int32_t> lru(cap);

	for (int i = 0; i < cap; i++) REQUIRE(not lru.set(i, i));

	for (int i = 0; i < cap; i++) {
		int32_t val;
		int32_t key = i;
		// false signifies success
		REQUIRE(not lru.get(val, key));
		REQUIRE(key == val);
	}
}

// Test that when adding 2x too many, only the latter half is resident
TEST_CASE( "TwiceToOneHalf", "[LruCache]" ) {
	constexpr int			   cap = 16;
	LruCache<int32_t, int32_t> lru(cap);

	for (int i = 0; i < cap * 2; i++) REQUIRE(not lru.set(i, i));

	for (int i = 0; i < cap; i++) {
		int32_t val = 0, key = i;
		REQUIRE(lru.get(val, key));
	}
	for (int i = cap; i < cap * 2; i++) {
		int32_t val = 0, key = i;
		REQUIRE(not lru.get(val, key));
	}
}

// We add 3/2x the capacity, but before adding the last 1/2, we touch
// the first 1/2. Those should be moved to the head of the list, as they were used
// more recently.
// Then it should be the middle 1/2 that was evicted.
TEST_CASE( "MoveToFront", "[LruCache]" ) {
	constexpr int			   cap = 16;
	LruCache<int32_t, int32_t> lru(cap);

	// Third half????
	// The name reflects the capacity.
	std::vector<int32_t> firstHalf;
	std::vector<int32_t> secondHalf;
	std::vector<int32_t> thirdHalf;

	for (int i = 0; i < cap / 2; i++) {
		firstHalf.push_back(i);
		secondHalf.push_back(i + cap / 2);
		thirdHalf.push_back(i + cap);
	}

	for (auto x : firstHalf) lru.set(x, x);
	for (auto x : secondHalf) lru.set(x, x);
	for (auto x : firstHalf) {
		int32_t val;
		lru.get(val, x);
	}
	for (auto x : thirdHalf) lru.set(x, x);

	// Now we should have halfs 1 & 3, but none from 2

	for (auto x : firstHalf) {
		int32_t val = 0;
		REQUIRE(not lru.get(val, x));
	}
	for (auto x : thirdHalf) {
		int32_t val = 0;
		REQUIRE(not lru.get(val, x));
	}
	// must fail!
	for (auto x : secondHalf) {
		int32_t val = 0;
		REQUIRE(lru.get(val, x));
	}
}
