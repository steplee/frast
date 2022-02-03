

######################
# Options
# Passing any value counts (only existence of flag is checked)
# Available options:
# 		DEBUG_RASTERIO
# 		DEBUG_PRINT
# 		NO_TIMING
# 		RELEASE
######################

CXX ?= clang++

debugFlags :=
#jpeg_libs := -l:libturbojpeg.a
jpeg_libs := -lturbojpeg
cv_libs :=  -I/usr/include/opencv4 -I/usr/local/include/opencv4 -lopencv_highgui -lopencv_core -lopencv_imgcodecs -lopencv_imgproc

# Libs to link into main library. OpenCV not needed unless debug rasterio is passed.
# (Currently, opencv is needed by some apps too, but will completely remove later)
all_libs := $(jpeg_libs)

ifdef DEBUG_RASTERIO
debugFlags := -DDEBUG_RASTERIO 
all_libs += $(cv_libs)
endif
ifdef DEBUG_PRINT
debugFlags += -DDEBUG_PRINT 
endif

ifdef NO_TIMING
TIMER_CFLAGS :=
else
TIMER_CFLAGS := -DUSE_TIMER -l:libfmt.a
endif

ifdef RELEASE
OPT ?= -O3 -march=native -DNDEBUG
else
OPT ?= -O3 -g -march=native
endif
#OPT := -O3 -g -DNDEBUG -march=native
#OPT := -O0 -g

ADDO_THREADS ?= 8
CONVERT_THREADS ?= 8
WRITER_NBUF ?= 8
defs := -DWRITER_NBUF=$(WRITER_NBUF)

HEADERS := $(wildcard src/*.h src/*.hpp src/utils/*.h src/utils/*.hpp)
gdal_libs := -I/usr/include/gdal -lgdal

#libs := $(all_libs) -l:liblmdb.a -lpthread
libs := $(all_libs) -llmdb -lpthread

pybind_flags := $(shell python3 -m pybind11 --includes)
py_lib ?= -L /usr/lib/x86_64-linux-gnu -l$(shell python3 -m sysconfig | grep -e "\sLDLIBRARY =" | awk -F '=' '{print $$2}' | xargs | head -c -4 | tail -c +4)

BASE_CFLAGS := -std=c++17 -I/usr/local/include/eigen3  $(libs) -fopenmp $(debugFlags)

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
	$(CXX) src/frastConvertGdal.cc -o $@ build/frast.a $(APP_CFLAGS) $(gdal_libs) $(defs) -DCONVERT_THREADS=$(CONVERT_THREADS)

build/frastAddo: $(HEADERS) src/frastAddo.cc build/frast.a
	$(CXX) src/frastAddo.cc -o $@ build/frast.a $(APP_CFLAGS) $(cv_libs) $(defs) -DADDO_THREADS=$(ADDO_THREADS)

build/frastMerge: $(HEADERS) src/frastMerge.cc build/frast.a
	$(CXX) src/frastMerge.cc -o $@ build/frast.a $(gdal_libs) $(APP_CFLAGS) $(defs)

build/frastInfo: $(HEADERS) src/frastInfo.cc build/frast.a
	$(CXX) src/frastInfo.cc -o $@ build/frast.a $(APP_CFLAGS)

build/dbTest: $(HEADERS) src/dbTest.cc build/frast.a
	$(CXX) src/dbTest.cc -o $@ build/frast.a $(APP_CFLAGS) $(cv_libs)

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
$(PREFIX)/include/frast/utils:
	mkdir -p $(PREFIX)/include/frast/utils

DIST_PKGS := $(shell python3 -c "import site; print(site.getsitepackages()[0])")

install: all $(PREFIX)/include/frast/utils
	cp build/frast.a $(PREFIX)/lib/libfrast.a
	cp src/db.h src/image.h -t $(PREFIX)/include/frast/
	cp src/utils/common.h src/utils/data_structures.hpp src/utils/solve.hpp src/utils/timer.hpp -t $(PREFIX)/include/frast/utils/
	cp build/frastpy.so $(DIST_PKGS)
