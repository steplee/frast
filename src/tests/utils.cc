
#include <gtest/gtest.h>
#include <unistd.h>
#include <algorithm>

#include "../utils/data_structures.hpp"

/* ===================================================
 *
 *
 *                  RingBuffer
 *
 *
 * =================================================== */

TEST(RingBuffer, Basic) {
	constexpr int cap = 16;

	RingBuffer<int32_t> rb(cap);
	EXPECT_TRUE(rb.empty());
	EXPECT_EQ(rb.size(), 0);

	// Push until full, then pop em off
	for (int i = 0; i < cap; i++) EXPECT_FALSE(rb.push_back(i));

	// We are now full. Trying to push again will fail
	EXPECT_TRUE(rb.isFull());
	EXPECT_TRUE(rb.size() == cap);
	EXPECT_TRUE(rb.push_back(1337));
	EXPECT_TRUE(rb.isFull());
	EXPECT_TRUE(rb.size() == cap);

	for (int i = 0; i < cap; i++) {
		int value;
		rb.pop_front(value);
		EXPECT_EQ(value, i);
	}

	// We should now be empty
	EXPECT_TRUE(rb.empty());

	// Now repeat the entire process
	for (int i = 0; i < cap; i++) rb.push_back(i);
	EXPECT_TRUE(rb.isFull());
	for (int i = 0; i < cap; i++) {
		int value;
		EXPECT_FALSE(rb.pop_front(value));
		EXPECT_EQ(value, i);
	}
	EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, SmallAndLarge) {
	{
		constexpr int		cap = 2;
		RingBuffer<int32_t> rb(2);
		EXPECT_FALSE(rb.push_back(0));
		EXPECT_FALSE(rb.push_back(1));
		EXPECT_TRUE(rb.push_back(2));
		int32_t val;
		EXPECT_FALSE(rb.pop_front(val));
		EXPECT_FALSE(rb.pop_front(val));
		EXPECT_TRUE(rb.pop_front(val));
		EXPECT_TRUE(rb.empty());
	}

	{
		constexpr int		cap = 1 << 20;
		RingBuffer<int32_t> rb(cap);
		// Do three rounds of filling/emptying
		for (int i = 0; i < 3; i++) {
			EXPECT_TRUE(rb.empty());
			for (int i = 0; i < cap; i++) EXPECT_FALSE(rb.push_back(i));
			EXPECT_TRUE(rb.push_back(0));  // fails
			EXPECT_TRUE(rb.isFull());
			int32_t val;
			for (int i = 0; i < cap; i++) EXPECT_FALSE(rb.pop_front(val));
			EXPECT_TRUE(rb.pop_front(val));	 // fails
			EXPECT_TRUE(rb.empty());
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
TEST(LruCache, SetAndGet) {
	constexpr int			   cap = 16;
	LruCache<int32_t, int32_t> lru(cap);

	for (int i = 0; i < cap; i++) EXPECT_FALSE(lru.set(i, i));

	for (int i = 0; i < cap; i++) {
		int32_t val;
		int32_t key = i;
		// false signifies success
		EXPECT_FALSE(lru.get(val, key));
		EXPECT_EQ(key, val);
	}
}

// Test that when adding 2x too many, only the latter half is resident
TEST(LruCache, TwiceToOneHalf) {
	constexpr int			   cap = 16;
	LruCache<int32_t, int32_t> lru(cap);

	for (int i = 0; i < cap * 2; i++) EXPECT_FALSE(lru.set(i, i));

	for (int i = 0; i < cap; i++) {
		int32_t val = 0, key = i;
		EXPECT_TRUE(lru.get(val, key));
	}
	for (int i = cap; i < cap * 2; i++) {
		int32_t val = 0, key = i;
		EXPECT_FALSE(lru.get(val, key));
	}
}

// We add 3/2x the capacity, but before adding the last 1/2, we touch
// the first 1/2. Those should be moved to the head of the list, as they were used
// more recently.
// Then it should be the middle 1/2 that was evicted.
TEST(LruCache, MoveToFront) {
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
		EXPECT_FALSE(lru.get(val, x));
	}
	for (auto x : thirdHalf) {
		int32_t val = 0;
		EXPECT_FALSE(lru.get(val, x));
	}
	// must fail!
	for (auto x : secondHalf) {
		int32_t val = 0;
		EXPECT_TRUE(lru.get(val, x));
	}
}
