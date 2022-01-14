

HEADERS := $(wildcard *.h *.hpp)
#SRCS := $(wildcard *.cc)
fstiff_srcs := image.cc db.cc
main_srcs := image.cc db.cc
convertGdal_srcs := convertGdal.cc
gdal_libs := -lgdal

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3 -I/usr/local/include/opencv4 -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llmdb -lpthread  -fopenmp -march=native


db.o: $(HEADERS) $(main_srcs)
	clang++ -std=c++17 db.cc -c -o $@ $(BASE_CFLAGS) -g -O3
image.o: $(HEADERS) $(main_srcs)
	clang++ -std=c++17 image.cc -c -o $@ $(BASE_CFLAGS) -g -O3
fstiff.a: db.o image.o
	ar rcs $@ db.o image.o

convertGdal: $(HEADERS) $(convertGdal_srcs) fstiff.a
	clang++ -std=c++17 $(convertGdal_srcs) -o $@ $(BASE_CFLAGS) -g -O3 $(gdal_libs) fstiff.a

dbTest: $(HEADERS) dbTest.cc fstiff.a
	clang++ -std=c++17 dbTest.cc -o dbTest -g -O3  $(BASE_CFLAGS) fstiff.a

image.ll: image.cc makefile
	clang++ -std=c++17 image.cc -S -emit-llvm -o image.ll $(BASE_CFLAGS) -O3  -fno-discard-value-names
image.s: image.cc makefile
	clang++ -std=c++17 image.cc -S -o image.s $(BASE_CFLAGS) -O3  -fno-discard-value-names

clean:
	rm fstiff.a image.ll image.s dbTest *.o
