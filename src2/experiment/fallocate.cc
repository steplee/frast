#include <cassert>
#include <thread>
#include <atomic>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <fmt/core.h>
#include <unordered_map>
#include <vector>

// Build:
//     clang++ ../src2/experiment/fallocate.cc -o fallocateTest -I _deps/dep_fmt-src/include/ -L _deps/dep_fmt-build/ -lfmtd
//
// Check fragmentation of output files:
//      filefrag testFallo*.txt
//
//   seems to give consistently random results when using write() (regardless of FALLOC_FL_INSERT_RANGE).
//   BUT: using fallocate to allocate space before writing helps a lot (there is only one extent!).
//

/*
 *
 *
 * This shows me that FALLOC_FL_INSERT_RANGE is potentially very useful.
 *
 * It also seems to work with an mmaped file, at least when single threaded
 * (How would it change with more threads?).
 *
 * When you mmap a file, you provide a length. If you access beyond the length, you get junk.
 * So if you increase the file size with fallocate, you either must:
 *		1) munmap and mmap again.
 *		2) mremap with MREMAP_MAYMOVE
 *		3) mmap just once, but giving a length that goes beyond the original file's length.
 *
 * Option (3) is clearly very easy to use, it requires basically no change to code.
 * You just mmap a larger range than the existing file contains, and you must never
 * access any data smaller than the current file (until the filesize is increased).
 *
 * This almost seems too good to be true. Assuming multi-threading does not complicate
 * the situation, the only downside I see is that some pointers are invalidated when you do
 * an insert range operation -- meaning that any virtual mem access must go through
 * some class that manages the base offsets of regions of the file.
 *
 * Perhaps it also increases fragmentation of the file.
 *
 *
 */




void print_word_occurances(const std::string& name) {
		int fd = open(name.c_str(), O_RDONLY);
		off_t sz = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		std::vector<char> txt(sz);
		ssize_t readSize = read(fd, txt.data(), sz);
		assert(readSize == sz);
		close(fd);

		std::unordered_map<std::string,int> cnt;
		int start = 0;
		bool bad = false;
		for (int i=0; i<readSize; i++) {
			if (txt[i] == '.' or txt[i] == ' ') {
				if (i > start+1 and not bad) {
					std::string s { &txt[start], (size_t)i-start };
					cnt[s] += 1;
				}
				start = i+1;
				bad = false;
			} else {
				bad |= txt[i] < '0' or txt[i] > 'z';
			}
		}
		fmt::print(" - Word Counts:\n");
		for (const auto& kv : cnt) {
			fmt::print("     - {:>10s}: {:>6d}\n", kv.first, kv.second);
		}
}
void print_word_occurances_buffer(char* txt, size_t size) {
		std::unordered_map<std::string,int> cnt;
		int start = 0;
		bool bad = false;
		for (int i=0; i<size; i++) {
			if (txt[i] == '.' or txt[i] == ' ') {
				if (i > start+1 and not bad) {
					std::string s { &txt[start], (size_t)i-start };
					cnt[s] += 1;
				}
				start = i+1;
				bad = false;
			} else {
				bad |= txt[i] < '0' or txt[i] > 'z';
			}
		}
		fmt::print(" - Word Counts (Buffer):\n");
		int i=0;
		for (const auto& kv : cnt) {
			fmt::print("     - {:>10s}: {:>6d}\n", kv.first, kv.second);
			i++;
			if (i >= 5) {
				fmt::print("     - ... truncating {} more...\n", cnt.size()-5);
				break;
			}
		}
}

void write_contiguous_file() {
	const char name[] = "testFalloc0.txt";
	const char buf1[] = "Header. ";
	const char buf2[] = "Body  . ";
	const char buf3[] = "Header2.";
	int nwritten = 0;
	int nheader = 0;
	int nbody = 0;
	int nmore = 0;

	{
		int fd = open(name, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH);
		assert(fd >= 0);

		fallocate(fd, 0, 0, 8192+16384+4096);

		for (int i=0; i<8192/8; i++)
			nheader += write(fd, buf1, 8);
		for (int i=0; i<16384/8; i++)
			nbody += write(fd, buf2, 8);
		for (int i=0; i<4096/8; i++)
			nmore += write(fd, buf3, 8);
		
		close(fd);
	}
}

//
// Test FALLOC_FL_INSERT_RANGE *without* mmap
// It works.
//
void run_test1() {
	const char name[] = "testFalloc1.txt";
	const char buf1[] = "Header. ";
	const char buf2[] = "Body  . ";
	const char buf3[] = "Header2.";
	int nwritten = 0;
	int nheader = 0;
	int nbody = 0;

	// Write original file.
	{
		int fd = open(name, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH);
		assert(fd >= 0);

		for (int i=0; i<8192/8; i++)
			nheader += write(fd, buf1, 8);
		for (int i=0; i<16384/8; i++)
			nbody += write(fd, buf2, 8);
		
		close(fd);
	}
	fmt::print(" - wrote {}, {}\n", nheader,nbody);
	fmt::print("\n");
	print_word_occurances(std::string{name});

	// Insert new data in the middle.
	{
		int fd = open(name, O_RDWR);
		assert(fd >= 0);

		int r = fallocate(fd, FALLOC_FL_INSERT_RANGE, 8192, 4096);
		assert(r == 0);

		r = lseek(fd, 8192, SEEK_SET);
		assert(r != -1);

		int nmore = 0;
		for (int i=0; i<4096/8; i++)
			nmore += write(fd, buf3, 8);
		fmt::print(" - wrote 4096 more bytes\n", nmore);

		close(fd);
	}
	fmt::print("\n");
	print_word_occurances(std::string{name});

}

//
// Test FALLOC_FL_INSERT_RANGE *while mmaping*
//
void run_test2(int mapMultiplier) {
	const char name[] = "testFalloc2.txt";
	const char buf1[] = "Header. ";
	const char buf2[] = "Body  . ";
	const char buf3[] = "Header2.";
	int nwritten = 0;
	int nheader = 0;
	int nbody = 0;
	int size1, size2;

	// Write original file.
	{
		int fd = open(name, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH);
		assert(fd >= 0);

		for (int i=0; i<8192/8; i++)
			nheader += write(fd, buf1, 8);
		for (int i=0; i<16384/8; i++)
			nbody += write(fd, buf2, 8);
		
		close(fd);
		size1 = nheader+nbody;
	}
	fmt::print(" - wrote {}, {}\n", nheader,nbody);
	fmt::print("\n");

	// Mmap original file.
	int mmap_fd;
	void* mmap_addr;
	{
		mmap_fd = open(name, O_RDWR);
		assert(mmap_fd >= 0);
		mmap_addr = mmap(0, size1*mapMultiplier, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
		assert(mmap_addr != 0);

		close(mmap_fd);
		mmap_fd = -1;
	}
	// print_word_occurances(std::string{name});
	print_word_occurances_buffer((char*)mmap_addr, size1);

	// Insert new data in the middle.
	{
		int fd = open(name, O_RDWR);
		assert(fd >= 0);

		int r = fallocate(fd, FALLOC_FL_INSERT_RANGE, 8192, 4096);
		assert(r == 0);

		r = lseek(fd, 8192, SEEK_SET);
		assert(r != -1);

		int nmore = 0;
		for (int i=0; i<4096/8; i++)
			nmore += write(fd, buf3, 8);
		fmt::print(" - wrote 4096 more bytes\n", nmore);

		close(fd);
		size2 = nheader+nbody+nmore;
	}

	fmt::print("\n - Finding words in mmap WITHOUT remapping:\n");
	print_word_occurances_buffer((char*)mmap_addr, size2);
	fmt::print("\n - Finding words in modified file (should work):\n");
	print_word_occurances(std::string{name});

	void* old_mmap_addr = mmap_addr;
	if (0) {
		auto r = munmap(mmap_addr, size1);
		assert(r == 0);

		mmap_fd = open(name, O_RDWR);
		assert(mmap_fd >= 0);
		mmap_addr = mmap(0, size2, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
		assert(mmap_addr != 0);

		close(mmap_fd);
		mmap_fd = -1;
	} else {
		// mmap_addr = mremap(mmap_addr, size1, size2, 0);
		mmap_addr = mremap(mmap_addr, size1, size2, MREMAP_MAYMOVE);
		assert(mmap_addr != (void*)-1);
		assert(mmap_addr != 0);
	}
	fmt::print(" - after modify mmap addr: {} -> {}\n", old_mmap_addr, mmap_addr);
	fmt::print("\n - Finding words in mmap after remapping:\n");
	print_word_occurances_buffer((char*)mmap_addr, size2);
}


// Idk what this is supposed to test...
void run_multithreaded_test() {

	const char name[] = "testFalloc3.txt";
	int bufSize = 4096;
	std::vector<char> buf(bufSize);
	for (int i=0; i<bufSize; i++)
		buf[i] = 'a' + (i%16);
	int writeLoops  = 1<<10;
	int initialSize = bufSize * 2;
	int insertPoint = bufSize;
	int finalSize   = bufSize * writeLoops;
	// int nwritten    = 0;
	std::atomic_int nwritten {0};

	// Write original file.
	int fd = open(name, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH);
	{
		assert(fd >= 0);
		nwritten += write(fd, buf.data(), bufSize);
		nwritten += write(fd, buf.data(), bufSize);
	}
	fmt::print(" - wrote {}\n", nwritten);

	void* mmap_addr;
	{
		int mmap_fd;
		mmap_fd = open(name, O_RDWR);
		assert(mmap_fd >= 0);
		mmap_addr = mmap(0, finalSize, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
		assert(mmap_addr != 0);

		close(mmap_fd);
		mmap_fd = -1;
	}

	std::thread readerThread([&](){
		while (nwritten < finalSize) {
			usleep(1'000);
			int nw = nwritten;
			int bad = 0;
			/*
			for (int i=0; i<nw; i++) {
				if (static_cast<char*>(mmap_addr)[i] != 'a'+(i%16)) {
					fmt::print("       {}{}{}{}{}\n",
						static_cast<char*>(mmap_addr)[i-2],
						static_cast<char*>(mmap_addr)[i-1],
						static_cast<char*>(mmap_addr)[i  ],
						static_cast<char*>(mmap_addr)[i+1],
						static_cast<char*>(mmap_addr)[i+2]);
					fmt::print(" - BAD    ^\n");
					bad++;
				}
			}
			*/
			fmt::print(" - tail {} {} {} {}\n",
						(int)static_cast<char*>(mmap_addr)[nw-4],
						(int)static_cast<char*>(mmap_addr)[nw-3],
						(int)static_cast<char*>(mmap_addr)[nw-2],
						(int)static_cast<char*>(mmap_addr)[nw-1]);
			fmt::print(" - obs {}, bad {}\n", nw, bad);
		}
	});


	for (int i=0; i<writeLoops; i++) {

		int r = fallocate(fd, FALLOC_FL_INSERT_RANGE, insertPoint, initialSize);
		if (r != 0) fmt::print(" - errno {}\n", strerror(errno));
		assert(r == 0);
		fmt::print(" - fallocate expand\n");

		/*
		// r = lseek(fd, initialSize, SEEK_SET);
		// r = lseek(fd, 0, SEEK_SET);
		assert(r != -1);

		int nmore = 0;
		nmore += write(fd, buf.data(), bufSize);
		nwritten += nmore;
		*/
		memcpy(static_cast<char*>(mmap_addr)+insertPoint, buf.data(), bufSize);
		nwritten += bufSize;
		fmt::print(" - wrote {} more bytes ({} total)\n", bufSize, nwritten);

		usleep(10'000);
	}


	readerThread.join();
	close(fd);
}


// What I wanted to see happen, happens.
// Yet it feels on _very_ shaky grounds...
// If it only causes file fragmentation I can live with that.
void run_multithreaded_test2() {

	const char name[] = "testFalloc3.txt";
	int bufSize = 4096;
	std::vector<char> bufA(bufSize);
	std::vector<char> bufB(bufSize);
	std::vector<char> bufC(bufSize);
	for (int i=0; i<bufSize; i++) bufA[i] = 'a';
	for (int i=0; i<bufSize; i++) bufB[i] = 'b';
	for (int i=0; i<bufSize; i++) bufC[i] = 'c';
	int initialSize = bufSize * 2;
	int finalSize   = bufSize * 3;
	int insertPoint = bufSize;
	// int nwritten    = 0;
	std::atomic_int nwritten {0};

	// Write original file.
	int fd = open(name, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH);
	{
		assert(fd >= 0);
		nwritten += write(fd, bufA.data(), bufSize);
		nwritten += write(fd, bufC.data(), bufSize);
	}
	fmt::print(" - wrote {}\n", nwritten);

	void* mmap_addr;
	{
		int mmap_fd;
		mmap_fd = open(name, O_RDWR);
		assert(mmap_fd >= 0);
		mmap_addr = mmap(0, finalSize, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
		assert(mmap_addr != 0);
		assert(mmap_addr != (void*)-1);

		close(mmap_fd);
		mmap_fd = -1;
	}

	int readerLoops = 20;
	std::thread readerThread([&](){
		for (int i=0; i<readerLoops; i++) {
		// while (nwritten < finalSize) {
			usleep(100'000);
			int nw = nwritten;
			int bad = 0;
			/*
			for (int i=0; i<nw; i++) {
				if (static_cast<char*>(mmap_addr)[i] != 'a'+(i%16)) {
					fmt::print("       {}{}{}{}{}\n",
						static_cast<char*>(mmap_addr)[i-2],
						static_cast<char*>(mmap_addr)[i-1],
						static_cast<char*>(mmap_addr)[i  ],
						static_cast<char*>(mmap_addr)[i+1],
						static_cast<char*>(mmap_addr)[i+2]);
					fmt::print(" - BAD    ^\n");
					bad++;
				}
			}
			*/

			if (nw > bufSize*2)
			fmt::print(" - (nw {}) Blocks (1 {}) (2 {}) (3 {})\n",
					nw,
						(int)static_cast<char*>(mmap_addr)[0],
						(int)static_cast<char*>(mmap_addr)[bufSize],
						(int)static_cast<char*>(mmap_addr)[bufSize*2]);
			else
			fmt::print(" - (nw {}) Blocks (1 {}) (2 {})\n",
					nw,
						(int)static_cast<char*>(mmap_addr)[0],
						(int)static_cast<char*>(mmap_addr)[bufSize]);

		}
	});


	for (int i=0; i<1; i++) {
		sleep(1);


		int r = fallocate(fd, FALLOC_FL_INSERT_RANGE, insertPoint, bufSize);
		if (r != 0) fmt::print(" - errno {}\n", strerror(errno));
		assert(r == 0);
		fmt::print(" - fallocate expand\n");

		// usleep(10'000);

		// sync_file_range(fd, 0, finalSize, 0);

		// mmap_addr = mremap(mmap_addr, previousSize, nwritten, MREMAP_MAYMOVE);
		// mmap_addr = mremap(mmap_addr, finalSize, finalSize, MREMAP_MAYMOVE);

		/*
		int mmap_fd = fd;
		// int mmap_fd;
		// mmap_fd = open(name, O_RDWR);
		// assert(mmap_fd >= 0);
		void *old = mmap_addr;
		r = munmap(mmap_addr, finalSize);
		if (r != 0) fmt::print(" - errno {}\n", strerror(errno));
		assert(r == 0);
		mmap_addr = mmap(0, finalSize, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
		assert(mmap_addr != 0);
		assert(mmap_addr != (void*)-1);
		// close(mmap_fd);
		// mmap_fd = -1;
		fmt::print(" - remap :: {} -> {}\n", old, mmap_addr);
		*/


		memcpy(static_cast<char*>(mmap_addr)+insertPoint, bufB.data(), bufSize);
		int previousSize = nwritten;
		nwritten += bufSize;
		fmt::print(" - wrote {} more bytes ({} total)\n", bufSize, nwritten);



		fmt::print(" - Writer Sees Blocks (1 {}) (2 {}) (3 {})\n",
					(int)static_cast<char*>(mmap_addr)[0],
					(int)static_cast<char*>(mmap_addr)[bufSize],
					(int)static_cast<char*>(mmap_addr)[bufSize*2]);

		

		// usleep(10'000);
	}


	readerThread.join();
	close(fd);
}

int main() {

	write_contiguous_file();

	fmt::print("\n*****************************\n");
	fmt::print("** Running Test 1 **\n");
	fmt::print("*****************************\n\n");
	run_test1();

	fmt::print("\n*****************************\n");
	fmt::print("** Running Test 2 (mapMultiplier = 1)**\n");
	fmt::print("*****************************\n\n");
	run_test2(1);

	fmt::print("\n*****************************\n");
	fmt::print("** Running Test 2 (mapMultiplier = 4)**\n");
	fmt::print("*****************************\n\n");
	run_test2(4);

	fmt::print("\n*****************************\n");
	fmt::print("** Running Multithreaded Test**\n");
	fmt::print("*****************************\n\n");
	// run_multithreaded_test();
	run_multithreaded_test2();

	return 0;
}
