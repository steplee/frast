

######################
# Options
# Passing any value counts (only existence of flag is checked)
# Available options:
# 		DEBUG_RASTERIO
# 		DEBUG_PRINT
# 		NO_TIMING
######################

CXX ?= clang++

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

OPT ?= -O3 -g -march=native
#OPT := -O3 -g -DNDEBUG -march=native
#OPT := -O0 -g



HEADERS := $(wildcard src/*.h src/*.hpp)
gdal_libs := -lgdal

libs := -lopencv_highgui -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llmdb -lpthread

pybind_flags := $(shell python3 -m pybind11 --includes)
py_lib := -L /usr/lib/x86_64-linux-gnu -l$(shell python3 -m sysconfig | grep -e "\sLDLIBRARY =" | awk -F '=' '{print $$2}' | xargs | head -c -4 | tail -c +4)

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3 -I/usr/local/include/opencv4 $(libs) -fopenmp $(debugFlags)

all: build build/frastConvertGdal build/frastAddo build/frastMerge build/frastInfo build/dbTest build/frastpy.so

build:
	mkdir build

######################
# Main lib
######################

APP_CFLAGS := $(BASE_CFLAGS) $(TIMER_CFLAGS) $(OPT)

build/db.o: $(HEADERS) src/db.cc
	$(CXX) -fPIC src/db.cc -c -o $@ $(APP_CFLAGS)
build/image.o: $(HEADERS) src/image.cc
	$(CXX) -fPIC src/image.cc -c -o $@ $(APP_CFLAGS)
build/frast.a: build/db.o build/image.o
	ar rcs $@ build/db.o build/image.o

######################
# Apps using main lib
######################

build/frastConvertGdal: $(HEADERS) src/frastConvertGdal.cc build/frast.a
	$(CXX) src/frastConvertGdal.cc -o $@ build/frast.a $(APP_CFLAGS) $(gdal_libs)

build/frastAddo: $(HEADERS) src/frastAddo.cc build/frast.a
	$(CXX) src/frastAddo.cc -o $@ build/frast.a $(APP_CFLAGS)

build/frastMerge: $(HEADERS) src/frastMerge.cc build/frast.a
	$(CXX) src/frastMerge.cc -o $@ build/frast.a $(gdal_libs) $(APP_CFLAGS)

build/frastInfo: $(HEADERS) src/frastInfo.cc build/frast.a
	$(CXX) src/frastInfo.cc -o $@ build/frast.a $(APP_CFLAGS)

build/dbTest: $(HEADERS) src/dbTest.cc build/frast.a
	$(CXX) src/dbTest.cc -o $@ build/frast.a $(APP_CFLAGS)

######################
# Python lib
######################

build/frastpy.so: build/frast.a src/frastPy.cc
	$(CXX) -fPIC src/frastPy.cc $(pybind_flags) $(py_lib) build/frast.a $(APP_CFLAGS) -shared -o $@

######################
# Some debug stuff.
######################

build/image.ll: src/image.cc makefile
	clang++ src/image.cc -S -emit-llvm -o $@ $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names
build/image.s: src/image.cc makefile
	clang++ src/image.cc -S -o $@ $(BASE_CFLAGS) $(OPT)  -fno-discard-value-names

clean:
	rm build/*


######################
# Install
######################

PREFIX ?= /usr/local

$(PREFIX)/include/frast:
	mkdir $(PREFIX)/include/frast

DIST_PKGS := $(shell python3 -c "import site; print(site.getsitepackages()[0])")

install: all $(PREFIX)/include/frast
	cp build/frast.a $(PREFIX)/lib/libfrast.a
	cp src/db.h src/image.h -t $(PREFIX)/include/frast/
	cp build/frastpy.so $(DIST_PKGS)
