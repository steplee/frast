#include <cassert>

#include "env.h"

namespace frast {






	PagedEnvironment::~PagedEnvironment() {
	}

	PagedEnvironment::PagedEnvironment(const std::string& path, const EnvOptions& opts)
		: BaseEnvironment<PagedEnvironment>(path, opts)
	{

		// Setup pointers.
		size_t sizeof_occ = (opts.mapSize / (8 * pageSize()));

		occPointer  = static_cast<uint8_t*>(basePointer);
		dataPointer = static_cast<char*>(basePointer) + sizeof_occ;

		// anon mmap actually zero-fills, but do anyway
		if (fileIsNew_ or opts.anon)
			bzero(occPointer, sizeof_occ);

		npages_ = mapSize_ / pageSize();

	}

	void* PagedEnvironment::allocatePages(size_t n) {

		// WARNING: This is fast, but bad for disk usage.
		// size_t start = occHead;
		// bool allowWrapAround = true;
		// WARNING: This is slow, but good for disk usage.
		//          See note about segment-tree free-list...
		size_t start = 0;
		bool allowWrapAround = false;



		bool wrappedAroundOnce = false;

		while (true) {

			if (start+n >= npages_) {
				// Re-try
				if (allowWrapAround and not wrappedAroundOnce) {
					start = 0;
					wrappedAroundOnce = true;
					continue;
				} else {
					throw std::runtime_error("exhausted mmap, or too large alloc.");
				}
			}

			bool failed = false;

			// Search for a contiguous span of pages that are not occupied
			for (size_t end=start; end<start+n; end++) {
				if (occ(end)) {
					// if we fail, move start to one-past failure point.
					start = end+1;
					failed = true;
				}
			}

			if (not failed) {
				// We've found a good span, starting at @start
				break;
			} else {
				// we already set the next @start
			}

		}

		for (size_t i=start; i<start+n; i++) {
			setOcc(i, true);
			// fmt::print(" - setOcc {}\n", i);
			// fmt::print(" - occ {}\n", occ(i));
		}

		occHead = start + n;
		void* out = static_cast<void*>(static_cast<char*>(dataPointer) + pageSize()*start);
		fmt::print(" - Allocating {} pages at {}-{} ret {}\n", n,start, start+n, out);

		if (not isAnonymous_)
			fallocate(fd_, 0, 0, start+n);

		return out;
	}

	void PagedEnvironment::freePages(void* firstByte_, size_t n) {
		char* first = static_cast<char*>(firstByte_);
		char* base  = static_cast<char*>(dataPointer);
		assert(first >= base);
		size_t diff = (first - base);
		// fmt::print(" - Freeing (base {}) (first {}) (n {}) (diff {})\n", (void*)base, (void*)first, n, diff);
		fmt::print(" - Freeing {} pages at {}\n", n, firstByte_);
		assert(diff % pageSize() == 0);
		size_t offsetPage = diff / pageSize();
		assert(offsetPage + n < npages_);

		for (size_t i=offsetPage; i<offsetPage+n; i++) {
			assert(occ(i));
			setOcc(i, false);
		}

		// This was a short-lived allocation.
		// We are free to wind the head back, which might help with fragmentation.
		if (offsetPage+n == occHead) {
			occHead -= n;
		}
	}

	void* PagedEnvironment::allocateBytes(size_t n, size_t alignment) {
		size_t npages = (n + pageSize() - 1) / pageSize();
		return allocatePages(npages);
	}
	void  PagedEnvironment::freeBytes(void* firstByte, size_t n) {
		size_t npages = (n + pageSize() - 1) / pageSize();
		freePages(firstByte, npages);
	}









	void* ArenaEnvironment::allocateBytes(size_t n, size_t alignment) {
		while (head % alignment != 0) head++;

		if (head+n >= mapSize_ - 4096)
			throw std::runtime_error("ArenaEnv out of mem.");

		void* out = static_cast<char*>(dataPointer) + head;
		head += n;

		// TODO: Maybe keep a cache version and do 1.5x sized allocations to reudce syscalls
		if (not isAnonymous_) {
			int r = fallocate(fd_, 0, 0, (char*)out + n - (char*)basePointer);
			// int r = ftruncate(fd_, (char*)out + n - (char*)basePointer);
			// int r = ftruncate(fd_, 1<<20);
			// fmt::print("(fallocate fd={} len={} -> {})\n", fd_, head, r);
		}

		fmt::print(" - (arena alloc {} len {}, head {})\n", out, n, head);
		return out;
	}
	void ArenaEnvironment::freeBytes(void* firstByte, size_t n) {
		fmt::print("(ArenaEnv::freeBytes() noop, head={})\n", head);
	}

	ArenaEnvironment::~ArenaEnvironment() {
		fmt::print(" - [~ArenaEnv] writing head {}\n", head);
		static_cast<uint64_t*>(basePointer)[0] = head;
		fmt::print(" - [~ArenaEnv] wrote   head {}\n", static_cast<uint64_t*>(basePointer)[0]);
	}

	ArenaEnvironment::ArenaEnvironment(const std::string& path, const EnvOptions& opts)
		: BaseEnvironment<ArenaEnvironment>(path, opts)
	{

		// Setup pointers.
		dataPointer = static_cast<void*>(static_cast<char*>(basePointer) + 4096);

		if (fileIsNew_ or opts.anon)
			head = 0;
		else
			head = static_cast<uint64_t*>(basePointer)[0];

		fmt::print(" - ArenaEnv with head @ {}\n", head);

	}

	void ArenaEnvironment::reset() {
		fmt::print(" - Warning: ArenaEnv reseting (without truncate)\n");
		// TODO: truncate this?
		head = 0;
	}





}
