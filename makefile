

CXX := clang++
#CXX := g++
HEADERS := $(wildcard *.h *.hpp)
#SRCS := $(wildcard *.cc)
main_srcs := image.cc db.cc
convertGdal_srcs := convertGdal.cc
gdal_libs := -lgdal

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3 -I/usr/local/include/opencv4 -lopencv_highgui -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llmdb -lpthread  -fopenmp -march=native
#OPT := -O3
OPT := -O0


######################
# Main lib
######################
db.o: $(HEADERS) $(main_srcs)
	$(CXX) -std=c++17 db.cc -c -o $@ $(BASE_CFLAGS) -g $(OPT)
image.o: $(HEADERS) $(main_srcs)
	$(CXX) -std=c++17 image.cc -c -o $@ $(BASE_CFLAGS) -g $(OPT)
fstiff.a: db.o image.o
	ar rcs $@ db.o image.o

######################
# Apps using main lib
######################
convertGdal: $(HEADERS) $(convertGdal_srcs) fstiff.a
	$(CXX) -std=c++17 $(convertGdal_srcs) -o $@ $(BASE_CFLAGS) -g $(OPT) fstiff.a $(gdal_libs)

frastAddo: $(HEADERS) frastAddo.cc fstiff.a
	$(CXX) -std=c++17 frastAddo.cc -o frastAddo -g $(OPT)  fstiff.a $(BASE_CFLAGS)
	#$(CXX) -std=c++17 frastAddo.cc -o frastAddo -g $(OPT)  db.o image.o $(BASE_CFLAGS)

dbTest: $(HEADERS) dbTest.cc fstiff.a
	$(CXX) -std=c++17 dbTest.cc -o dbTest -g $(OPT)  fstiff.a $(BASE_CFLAGS)


######################
# Some debug stuff.
######################
image.ll: image.cc makefile
	clang++ -std=c++17 image.cc -S -emit-llvm -o image.ll $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names
image.s: image.cc makefile
	clang++ -std=c++17 image.cc -S -o image.s $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names

clean:
	rm fstiff.a image.ll image.s dbTest *.o fstiff convertGdal
