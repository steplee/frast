

######################
# Options
# Passing any value counts (only existence of flag is checked)
# Available options:
# 		DEBUG_RASTERIO
# 		DEBUG_PRINT
# 		NO_TIMING
######################

CXX := clang++

debugFlags :=
ifdef DEBUG_RASTERIO
debugFlags := -DDEBUG_RASTERIO 
endif
ifdef DEBUG_PRINT
debugFlags += -DDEBUG_PRINT 
endif

ifdef NO_TIMING
TIMER_CFLAGS :=
else
TIMER_CFLAGS := -DUSE_TIMER -lfmt
endif

OPT := -O3 -g -march=native
#OPT := -O3 -g -DNDEBUG -march=native
#OPT := -O0 -g



#CXX := g++
HEADERS := $(wildcard *.h *.hpp)
main_srcs := image.cc db.cc
frastConvertGdal_srcs := frastConvertGdal.cc
gdal_libs := -lgdal

libs := -lopencv_highgui -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llmdb -lpthread  

pybind_flags := $(shell python3 -m pybind11 --includes)
py_lib := -L /usr/lib/x86_64-linux-gnu -l$(shell python3 -m sysconfig | grep -e "\sLDLIBRARY =" | awk -F '=' '{print $$2}' | xargs | head -c -4 | tail -c +4)

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3 -I/usr/local/include/opencv4 $(libs) -fopenmp $(debugFlags)

all: frastConvertGdal frastAddo frastMerge frastInfo dbTest frastpy.so

######################
# Main lib
######################

APP_CFLAGS := $(BASE_CFLAGS) $(TIMER_CFLAGS) $(OPT)

db.o: $(HEADERS) db.cc
	$(CXX) -fPIC db.cc -c -o $@ $(APP_CFLAGS)
image.o: $(HEADERS) image.cc
	$(CXX) -fPIC image.cc -c -o $@ $(APP_CFLAGS)
frast.a: db.o image.o
	ar rcs $@ db.o image.o

######################
# Apps using main lib
######################


frastConvertGdal: $(HEADERS) $(frastConvertGdal_srcs) frast.a
	$(CXX) $(frastConvertGdal_srcs) -o $@ frast.a $(APP_CFLAGS) $(gdal_libs)

frastAddo: $(HEADERS) frastAddo.cc frast.a
	$(CXX) frastAddo.cc -o frastAddo frast.a $(APP_CFLAGS)

frastMerge: $(HEADERS) frastMerge.cc frast.a
	$(CXX) frastMerge.cc -o $@ frast.a $(gdal_libs) $(APP_CFLAGS)

frastInfo: $(HEADERS) frastInfo.cc frast.a
	$(CXX) frastInfo.cc -o frastInfo frast.a $(APP_CFLAGS)

dbTest: $(HEADERS) dbTest.cc frast.a
	$(CXX) dbTest.cc -o dbTest frast.a $(APP_CFLAGS)

######################
# Python lib
######################

frastpy.so: frast.a frastPy.cc
	$(CXX) -fPIC frastPy.cc $(pybind_flags) $(py_lib) frast.a $(APP_CFLAGS) -shared -o $@

######################
# Some debug stuff.
######################

image.ll: image.cc makefile
	clang++ image.cc -S -emit-llvm -o image.ll $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names
image.s: image.cc makefile
	clang++ image.cc -S -o image.s $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names

clean:
	rm frast.a image.ll image.s dbTest *.o frastConvertGdal frastAddo frastMerge frastpy.so frastInfo
