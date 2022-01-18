

CXX := clang++
#CXX := g++
HEADERS := $(wildcard *.h *.hpp)
#SRCS := $(wildcard *.cc)
main_srcs := image.cc db.cc
frastConvertGdal_srcs := frastConvertGdal.cc
gdal_libs := -lgdal

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3 -I/usr/local/include/opencv4 -lopencv_highgui -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llmdb -lpthread  -fopenmp -march=native
OPT := -O3
#OPT := -O0


######################
# Main lib
######################

db.o: $(HEADERS) $(main_srcs)
	$(CXX) -std=c++17 db.cc -c -o $@ $(BASE_CFLAGS) -g $(OPT)
image.o: $(HEADERS) $(main_srcs)
	$(CXX) -std=c++17 image.cc -c -o $@ $(BASE_CFLAGS) -g $(OPT)
frast.a: db.o image.o
	ar rcs $@ db.o image.o

######################
# Apps using main lib
######################

frastConvertGdal: $(HEADERS) $(frastConvertGdal_srcs) frast.a
	$(CXX) -std=c++17 $(frastConvertGdal_srcs) -o $@ $(BASE_CFLAGS) -g $(OPT) frast.a $(gdal_libs)

frastAddo: $(HEADERS) frastAddo.cc frast.a
	$(CXX) -std=c++17 frastAddo.cc -o frastAddo -g $(OPT)  frast.a $(BASE_CFLAGS)
	#$(CXX) -std=c++17 frastAddo.cc -o frastAddo -g $(OPT)  db.o image.o $(BASE_CFLAGS)

frastMerge: $(HEADERS) frastMerge.cc frast.a
	$(CXX) -std=c++17 frastMerge.cc -o $@ $(BASE_CFLAGS) -g $(OPT) frast.a $(gdal_libs)

dbTest: $(HEADERS) dbTest.cc frast.a
	$(CXX) -std=c++17 dbTest.cc -o dbTest -g $(OPT)  frast.a $(BASE_CFLAGS)


######################
# Some debug stuff.
######################

image.ll: image.cc makefile
	clang++ -std=c++17 image.cc -S -emit-llvm -o image.ll $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names
image.s: image.cc makefile
	clang++ -std=c++17 image.cc -S -o image.s $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names

clean:
	rm frast.a image.ll image.s dbTest *.o frast frastConvertGdal
