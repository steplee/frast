
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cerrno>

#include <fmt/core.h>


int main() {

	int  pageSize1 = getpagesize();
	long pageSize2 = sysconf(_SC_PAGESIZE);
	fmt::print(" - page size: {}\n", pageSize1);
	fmt::print(" - page size: {}\n", pageSize2);

	void *mem = mmap(nullptr, pageSize1, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (mem == (void*)-1) {
		fmt::print(" - mmap failed with: {}, {}\n", errno, strerror(errno));
	}
	fmt::print(" - mem: {}\n", mem);

	return 0;
}
