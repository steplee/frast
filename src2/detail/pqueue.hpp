#pragma once

#include <vector>
#include <string>

#include <fmt/core.h>

namespace frast {

	//
	// Pretty vanilla heap-based implementation of a PQ
	//
	template <typename K>
	class PriorityQueue {

		public:
			void add(const K& k);
			K pop();
			int size() const;

		private:
			std::vector<K> ks;

	};

	template <typename K>
	inline int PriorityQueue<K>::size() const {
		return ks.size();
	}
	
	template <typename K>
	inline void PriorityQueue<K>::add(const K& k) {
		ks.push_back(k);
		int i = ks.size() - 1;
		int j = i >> 1;

		while (i > 0 and ks[j] < ks[i]) {
			std::swap(ks[i], ks[j]);
			i = j;
			j = j >> 1;
		}

		// fmt::print(" - added {}, array is now", k); for (auto i : ks) fmt::print(" {}", i); fmt::print("\n");
	}

	template <typename K>
	inline K PriorityQueue<K>::pop() {
		K out { std::move(ks[0]) };

		if (ks.size() > 1) {
			std::swap(ks[0], ks[ks.size()-1]);
			ks.pop_back();

			// fmt::print(" - popping {}, array is now", out); for (auto i : ks) fmt::print(" {}", i); fmt::print("\n");

			int i = 0;
			int j = i*2+0;
			if (i*2+1 < ks.size() and ks[i*2+1] > ks[i*2+0]) j = i*2+1;

			while (ks[i] < ks[j]) {
				std::swap(ks[i], ks[j]);
				// fmt::print(" - popping {}, array is now", out); for (auto i : ks) fmt::print(" {}", i); fmt::print("\n");
				i = j;

				j = i*2+0;
				if (j >= ks.size()) break;
				if (i*2+1 < ks.size() and ks[i*2+1] > ks[i*2+0]) j = i*2+1;
			}
		}

		return out;

	}

}

