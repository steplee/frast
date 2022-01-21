#pragma once

#include <unordered_map>
#include <deque>
#include <vector>

#include <cstdio>


// TODO: Test this a bit more rigorously
template <class K, class V>
class LruCache {
	protected:

		int capacity;
		std::deque<K> lst;

		struct ValueAndIdx {
			V v;
			typename decltype(lst)::iterator i;
		};

		std::unordered_map<K, ValueAndIdx> map;

	public:

		inline LruCache(int cap) : capacity(cap) {}
		inline LruCache() : capacity(0) {}

		// Return true if hit cache
		inline bool get(V& out, const K& k) const {
			auto it = map.find(k);
			if (it == map.end()) return false;
			out = it->second.v;
			return true;
		}

		// Return true if key was in cache.
		inline bool set(const K& k, const V& v) {
			auto it = map.find(k);
			assert(map.size() == lst.size());
			if (it == map.end()) {
				// Maybe pop oldest, then add new.
				if (lst.size() >= capacity) {
					//printf(" - (LruCache) Evicting lru entry at back (cache at capacity %d)\n", lst.size());
					auto remove_k = lst.back();
					map.erase(remove_k);
					lst.pop_back();
				}
				lst.push_front(k);
				map.insert({k, {v, lst.begin()}});
				return false;
			} else {
				// Must move to front.
				//printf(" - (LruCache) moving touched entry from %ld to front.\n", std::distance(lst.begin(), it->second.i));
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
	uint32_t cap, w_idx=0, r_idx=0;
	RingBuffer() : cap(0) { }
	RingBuffer(int cap_) : cap(cap_) {
		data.resize(cap);
	}
	inline bool pop_front(T& t) {
		if (r_idx == w_idx) return false;
		t = data[r_idx % cap];
		//printf(" - ring buffer pop_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		r_idx++;
		return true;
	}
	inline bool push_back(const T& t) {
		//if (w_idx - r_idx >= cap) return false;
		if (w_idx - r_idx >= cap) { assert(false); }
		data[w_idx % cap] = t;
		//printf(" - ring buffer push_front() idx %d val %d\n", r_idx, t); fflush(stdout);
		w_idx++;
		return true;
	}
	inline int size()  const { return w_idx -  r_idx; }
	inline bool empty() const { return w_idx == r_idx; }
	inline bool isFull() const { return w_idx - r_idx >= cap; }
};

