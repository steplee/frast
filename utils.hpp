#pragma once

#include <unordered_map>
#include <list>
#include <deque>
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

