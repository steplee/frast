

CXX := clang++
#CXX := g++
HEADERS := $(wildcard *.h *.hpp)
#SRCS := $(wildcard *.cc)
main_srcs := image.cc db.cc
frastConvertGdal_srcs := frastConvertGdal.cc
gdal_libs := -lgdal

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3 -I/usr/local/include/opencv4 -lopencv_highgui -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llmdb -lpthread  -fopenmp -march=native
OPT := -O3 -g
#OPT := -O0 -g

pybind_flags := $(shell python3 -m pybind11 --includes)
py_lib := -L /usr/lib/x86_64-linux-gnu -l$(shell python3 -m sysconfig | grep -e "\sLDLIBRARY =" | awk -F '=' '{print $$2}' | xargs | head -c -4 | tail -c +4)

######################
# Main lib
######################

db.o: $(HEADERS) db.cc
	$(CXX) -fPIC db.cc -c -o $@ $(BASE_CFLAGS) $(OPT)
image.o: $(HEADERS) image.cc
	$(CXX) -fPIC image.cc -c -o $@ $(BASE_CFLAGS) $(OPT)
frast.a: db.o image.o
	ar rcs $@ db.o image.o

######################
# Apps using main lib
######################

frastConvertGdal: $(HEADERS) $(frastConvertGdal_srcs) frast.a
	$(CXX) $(frastConvertGdal_srcs) -o $@ $(BASE_CFLAGS) $(OPT) frast.a $(gdal_libs)

frastAddo: $(HEADERS) frastAddo.cc frast.a
	$(CXX) frastAddo.cc -o frastAddo $(OPT)  frast.a $(BASE_CFLAGS)
	#$(CXX) frastAddo.cc -o frastAddo $(OPT)  db.o image.o $(BASE_CFLAGS)

frastMerge: $(HEADERS) frastMerge.cc frast.a
	$(CXX) frastMerge.cc -o $@ $(BASE_CFLAGS) $(OPT) frast.a $(gdal_libs)

frastInfo: $(HEADERS) frastInfo.cc frast.a
	$(CXX) frastInfo.cc -o frastInfo $(OPT)  frast.a $(BASE_CFLAGS)

dbTest: $(HEADERS) dbTest.cc frast.a
	$(CXX) dbTest.cc -o dbTest $(OPT)  frast.a $(BASE_CFLAGS)

######################
# Python lib
######################

frastpy.so: frast.a frastPy.cc
	$(CXX) -fPIC frastPy.cc $(pybind_flags) $(py_lib) $(OPT) $(BASE_CFLAGS) -shared -o $@ frast.a

######################
# Some debug stuff.
######################

image.ll: image.cc makefile
	clang++ image.cc -S -emit-llvm -o image.ll $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names
image.s: image.cc makefile
	clang++ image.cc -S -o image.s $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names

clean:
	rm frast.a image.ll image.s dbTest *.o frast frastConvertGdal frastAddo frastMerge frastPy.so
