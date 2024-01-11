#pragma once

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/color.h>

#include <string>
#include <cstdint>
#include <cassert>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cerrno>
#include <sys/types.h>
#include <fcntl.h>

namespace frast {

	//
	// Models an mmaped file.
	// Manages a page-allocation system.
	// The user must track how many pages they allocate.
	//
	// TODO: The linear-search-from-occHead is not very good because it is VITAL
	//       that we re-use freed memory (otherwise disk-use sky rockets)
	//       So you might always scan from start and look for a contiguous set of free blocks.
	//       (Using uint8_t arithmetic -- not bit arithmetic)
	//       A free-list or something could be used.
	//       This could be an-in memory segment tree that is never saved to disk.
	//       We only need it if the Environment supports writes.
	//
	// TODO:
	// Even if this works, we probably want a *seperate* memory region for the
	// tree structure nodes and a seperate region for the data.
	//

	struct EnvOptions {
		size_t mapOffset = 0;
		size_t mapSize = 64lu * (1lu<<30lu);
		// Used for easier testing: ignores @path and does not back memory to disk.
		bool anon = false;
		bool readonly = false;
		bool isTerrain = false;
		bool cache = true;

		static EnvOptions getReadonly(bool terrain=false) {
			EnvOptions o;
			o.isTerrain = terrain;
			o.readonly = true;
			return o;
		}
	};

	template <class Derived>
	class BaseEnvironment {
		public:


			BaseEnvironment(const std::string& path, const EnvOptions& opts);
			~BaseEnvironment();

			// size_t pageSize(size_t n);
			static inline constexpr size_t pageSize() { return 4096; } // TODO: get this from syscall

			inline size_t mapSize() { return mapSize_; }

			// NOTE: These are the public API functions. Derived should implement.
			/*
			void* allocateBytes(size_t n);
			void  freeBytes(void* firstByte, size_t n);
			*/

			inline bool fileIsNew() const { return fileIsNew_; }

			inline int getFd() const { return fd_; }
			inline void* getBasePointer() const { return basePointer; }


		protected:

			size_t mapSize_;
			bool fileIsNew_;
			bool isAnonymous_;
			int fd_=-1;

			// This is the mmap() base pointer
			void* basePointer = 0;


	};
	
	template <class Derived>
	BaseEnvironment<Derived>::~BaseEnvironment() {

		// fmt::print(" - [~BaseEnv] closing fd\n");
		if (fd_ >= 0) {
			int stat = close(fd_);
			if (stat == -1) {
				fmt::print(" - close failed with: {}, {}\n", errno, strerror(errno));
				// throw std::runtime_error("close failed.");
				assert(false);
				exit(1);
			}
			fd_ = -1;
		}

		assert(basePointer != 0);
		assert(mapSize_ > 0);
		// This will flush all pages. Could use msync to do that at other times.
		munmap(basePointer, mapSize_);
		basePointer = nullptr;
		mapSize_ = 0;
	}


	template <class Derived>
	BaseEnvironment<Derived>::BaseEnvironment(const std::string& path, const EnvOptions& opts) {
		// auto flags = MAP_PRIVATE;
		auto flags = MAP_SHARED;
		if (opts.anon) flags |= MAP_ANONYMOUS;
		auto prot = PROT_READ;
		if (not opts.readonly) prot |= PROT_WRITE;


		size_t offset = opts.mapOffset;

		fileIsNew_ = opts.anon;
		isAnonymous_ = opts.anon;

		if (not opts.anon) {
			struct stat statbuf;
			int res = ::stat(path.c_str(), &statbuf);
			if (res == -1 and errno == ENOENT) {
				fileIsNew_ = true;
			} else if (res == -1) {
				fmt::print(" - stat failed with: {}, {}\n", errno, strerror(errno));
				throw std::runtime_error("stat failed.");
			}

			if (opts.readonly and fileIsNew_) {
				throw std::runtime_error(fmt::format("If opts.readonly is true, file '{}' must already exist! ", path));
			}

			auto flags = opts.readonly ? O_RDONLY : O_RDWR;
			if (not opts.readonly) flags |= O_CREAT;
			auto mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
			if (opts.readonly) mode = S_IRUSR | S_IROTH | S_IRGRP;
			fd_ = open(path.c_str(), flags, mode);
			if (fd_ == -1) {
				fmt::print(" - open failed with: {}, {}\n", errno, strerror(errno));
				throw std::runtime_error("open failed.");
			}
		}

		basePointer = mmap(nullptr, opts.mapSize, prot, flags, fd_, offset);
		if (basePointer == (void*)-1) {
			fmt::print(" - mmap failed with: {}, {}\n", errno, strerror(errno));
			throw std::runtime_error("mmap failed.");
		}
		// fmt::print(" - basePointer: {}\n", basePointer);

		// int err = madvise(basePointer, opts.mapSize, MADV_SEQUENTIAL);
		// assert(err == 0);


		mapSize_ = opts.mapSize;
	}




















	//
	// WARNING: This requires an occupancy array sized:  mapSize / (4096 * 8).
	// So a 32 Gb map requires 1Mb of header space (the 'occ' pointer).
	// So that it s a downside (should use an array of sorted ranges...)
	//

	class PagedEnvironment : public BaseEnvironment<PagedEnvironment> {
		public:

			PagedEnvironment(const std::string& path, const EnvOptions& opts);
			~PagedEnvironment();

			// NOTE: Alignment is IGNORED because we always allocate along page boundaries...
			void* allocateBytes(size_t n, size_t alignment=1);
			void  freeBytes(void* firstByte, size_t n);


			// Only public for testing...
			inline bool occ(size_t pageNumber) const {
				size_t element = pageNumber / 8;
				size_t offset  = pageNumber % 8;
				// fmt::print(" - occ {} {} -> {}\n", element,offset, (int)occPointer[element]);
				return (occPointer[element] & (1<<offset)) != 0;
			}

			inline size_t npages() { return npages_; }

		// private:
		public:

			void* allocatePages(size_t n);
			void  freePages(void* firstByte, size_t n);

			size_t npages_;

			// Tells us which pages are mapped. A bit-set.
			uint8_t* occPointer = 0;

			// Points to actual usable data (after headers + occ and such)
			void* dataPointer = 0;

			// Points to first element that occ(i) is false.
			// We also have to linear search in worst case, but this should help often.
			size_t occHead = 0;

			inline void setOcc(size_t pageNumber, bool value) {
				assert(pageNumber < npages_);
				size_t element = pageNumber / 8;
				size_t offset  = pageNumber % 8;
				if (value == false) occPointer[element] &= (0xff - (1<<offset));
				else                occPointer[element] |= (1<<offset);
				// fmt::print(" - setOcc {} {} -> {}\n", element,offset, (int)occPointer[element]);
			}
	};



	// Does not support deletes ... free does nothing!
	class ArenaEnvironment : public BaseEnvironment<ArenaEnvironment> {
		public:

			ArenaEnvironment(const std::string& path, const EnvOptions& opts);
			~ArenaEnvironment();

			void* allocateBytes(size_t n, size_t alignment=1);
			void  freeBytes(void* firstByte, size_t n);

			void reset();

		// private:
		public:

			// Points to actual usable data (after 4096 byte header)
			void* dataPointer = 0;

			// Points to next allocation byte offset.
			// Stored as first 8 bytes in mmap.
			size_t head = 0;

	};





}
