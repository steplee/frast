#pragma once

// #include <deque>
#include <list>
#include <unordered_map>
#include <vector>
// #include <fmt/core.h>

template <class K, class V>
class LruCache {
  protected:

	// You CANNOT use deque here, because erase() somewhere in the
	// middle invalidates *all* iterators.
	// Was an annoying bug...
	// std::deque<K> lst;
	std::list<K> lst;

	struct ValueAndIdx {
		using It = typename decltype(lst)::iterator;
		V  v;
		It i;

		inline ValueAndIdx(const V& v, const It& i) : v(v), i(i) {
			// printf(" - (ValueAndIdx) copy const\n");
		}
		inline ValueAndIdx(V&& v, const It&& i) : v(v), i(i) {
			// printf(" - (ValueAndIdx) move const\n");
		}
		inline ValueAndIdx(ValueAndIdx&& o) {
			// printf(" - (ValueAndIdx) move assn\n");
			v = std::move(o.v);
			i = o.i;
		}
		inline ValueAndIdx& operator=(ValueAndIdx&& o) {
			// printf(" - (ValueAndIdx) move assn\n");
			v = std::move(o.v);
			i = o.i;
			return *this;
		}
	};

	std::unordered_map<K, ValueAndIdx> map;
	int			  capacity;

  public:
	inline LruCache(int cap) : capacity(cap) {}
	inline LruCache() : capacity(0) {}

	// Return true if key not in cache (failure)
	// Otherwise, move to front and return value
	inline bool get(V& out, const K& k) {
		auto it = map.find(k);
		if (it == map.end()) return true;

		// We have the entry in the cache.
		// We must move it to front if it is not already.
		if (it->second.i != lst.begin()) {
			lst.erase(it->second.i);
			lst.push_front(k);
			it->second.i = lst.begin();
			// fmt::print(" - pushed already existent index, moving to front : {} {} {}",
			// for (auto lit=lst.begin(); lit!=lst.end(); lit++) map[*lit].i = lit;
		}

		// Note: This is a copy
		out = it->second.v;

		return false;
	}

	// Return true if key was in cache.
	inline bool set(const K& k, const V& v) {
		auto it = map.find(k);
		// assert(map.size() == lst.size());
		if (it == map.end()) {
			// Maybe pop oldest, then add new.
			if (lst.size() >= capacity) {
				// Avoid reallocations by re-using buffer that was removed.

				// printf(" - (LruCache) Evicting lru entry at back (cache at capacity %d)\n", lst.size());
				auto remove_k = lst.back();

				auto it	  = map.find(remove_k);
				auto node = map.extract(it);
				// auto reuse = map.erase(map.find(remove_k));
				lst.pop_back();

				// Call the copy assignment operator (but *NO* constructors)
				// printf(" - Copying val to old val.\n"); fflush(stdout);
				node.mapped().v = v;

				lst.push_front(k);
				node.key()		= k;
				node.mapped().i = lst.begin();
				map.insert(std::move(node));

				/*
				auto it	  = map.find(remove_k);
				map.erase(it);
				lst.pop_back();
				lst.push_front(k);
				map.emplace(std::make_pair(k, ValueAndIdx{v, lst.begin()}));
				*/

			} else {
				// printf(" - not full, allocating new val.\n");
				lst.push_front(k);
				// map.emplace(std::make_pair(k, ValueAndIdx{v, lst.begin()}));
				map.emplace(std::make_pair(k, ValueAndIdx{v, lst.begin()}));
			}
			return false;
		} else {
			// Must move to front.
			// printf(" - (LruCache) moving touched entry from %ld to front.\n", std::distance(lst.begin(),
			// it->second.i));
			lst.erase(it->second.i);
			lst.push_front(k);
			it->second.i = lst.begin();
			return true;
		}
	}
};

template <class T>
struct RingBuffer {
	std::vector<T> data;
	uint32_t	   cap, w_idx = 0, r_idx = 0;
	RingBuffer() : cap(0) {}
	RingBuffer(int cap_) : cap(cap_) { data.resize(cap); }
	inline bool pop_front(T& t) {
		if (r_idx == w_idx) return true;
		t = data[r_idx % cap];
		// printf(" - ring buffer pop_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		r_idx++;
		return false;
	}
	inline bool push_back(const T& t) {
		if (w_idx - r_idx >= cap) return true;
		// if (w_idx - r_idx >= cap) { assert(false); }
		data[w_idx % cap] = t;
		// printf(" - ring buffer push_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		w_idx++;
		return false;
	}
	inline int	size() const { return w_idx - r_idx; }
	inline bool empty() const { return w_idx == r_idx; }
	inline bool isFull() const { return w_idx - r_idx >= cap; }
};
