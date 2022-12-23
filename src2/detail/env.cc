#include <cassert>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cerrno>

#include "env.h"

namespace frast {

	template <class Derived>
	BaseEnvironment<Derived>::~BaseEnvironment() {
		assert(basePointer != 0);
		assert(mapSize_ > 0);
		// This will flush all pages. Could use msync to do that at other times.
		munmap(basePointer, mapSize_);
	}

	template <class Derived>
	BaseEnvironment<Derived>::BaseEnvironment(const std::string& path, const EnvOptions& opts) {
		auto flags = MAP_PRIVATE;
		if (opts.anon) flags |= MAP_ANONYMOUS;
		auto prot = PROT_READ;
		if (not opts.readonly) prot |= PROT_WRITE;

		int fd = -1;
		size_t offset = opts.mapOffset;

		fileIsNew_ = false;

		if (not opts.anon) {
			struct stat statbuf;
			int res = ::stat(path.c_str(), &statbuf);
			if (res == -1 and errno == ENOENT) {
				fileIsNew_ = true;
			} else if (res == -1) {
				fmt::print(" - stat failed with: {}, {}\n", errno, strerror(errno));
				throw std::runtime_error("stat failed.");
			}

			auto flags = opts.readonly ? O_RDONLY : O_RDWR;
			flags |= O_CREAT;
			auto mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
			fd = open(path.c_str(), flags, mode);
			if (fd == -1) {
				fmt::print(" - open failed with: {}, {}\n", errno, strerror(errno));
				throw std::runtime_error("open failed.");
			}
		}

		basePointer = mmap(nullptr, opts.mapSize, prot, flags, fd, offset);
		if (basePointer == (void*)-1) {
			fmt::print(" - mmap failed with: {}, {}\n", errno, strerror(errno));
			throw std::runtime_error("mmap failed.");
		}
		fmt::print(" - basePointer: {}\n", basePointer);

		if (fd >= 0) {
			int stat = close(fd);
			if (stat == -1) {
				fmt::print(" - close failed with: {}, {}\n", errno, strerror(errno));
				throw std::runtime_error("close failed.");
			}
		}

		mapSize_ = opts.mapSize;
	}





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

	void* PagedEnvironment::allocateBytes(size_t n) {
		size_t npages = (n + pageSize() - 1) / pageSize();
		return allocatePages(npages);
	}
	void  PagedEnvironment::freeBytes(void* firstByte, size_t n) {
		size_t npages = (n + pageSize() - 1) / pageSize();
		freePages(firstByte, npages);
	}









	void* ArenaEnvironment::allocateBytes(size_t n) {
		if (head+n >= mapSize_ - 4096)
			throw std::runtime_error("ArenaEnv out of mem.");

		void* out = static_cast<char*>(dataPointer) + head;
		head += n;
		return out;
	}
	void ArenaEnvironment::freeBytes(void* firstByte, size_t n) {
		fmt::print("(ArenaEnv::freeBytes() noop, head={})\n", head);
	}

	ArenaEnvironment::~ArenaEnvironment() {
		static_cast<uint64_t*>(basePointer)[0] = head;
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
