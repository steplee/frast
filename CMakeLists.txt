cmake_minimum_required(VERSION 2.20)
project(frast)

# set(CMAKE_CXX_FLAGS "-std=c++17 -gdwarf-2 -O3 -Werror=return-type -fsanitize=address")
set(CMAKE_CXX_FLAGS "-std=c++17 -g -O3 -Werror=return-type")
# set(CMAKE_CXX_FLAGS "-std=c++17 -O1 -Werror=return-type -fno-omit-frame-pointer -fno-inline -fno-inline-functions")

# set(CMAKE_CXX_FLAGS "-std=c++17 -gdwarf-2 -O0 -Werror=return-type")
# set(CMAKE_CXX_FLAGS_DEBUG)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(libsZ "z")

#####################
# Options
#####################

# I thought I could have a completely independent (and private) usage of OpenCV.
# But once again, the Unix linking experience has been awful and this was not
# easy. I do not recommend turning this on, because if a user of the libfrast2.a
# includes a different opencv version, problems occur.
#
# What I wanted was to just copy all needed symbols from the static opencv libs and make them private.
# Kind of like inlining functions. But instead you get undefined references and this forces you to pass
# the same opencv libs to link with, destroying the encapsulation.
option(TESTS "Build Tests" ON)
option(STATIC_OPENCV "Force opencv static libs" OFF)

option(ENABLE_IMGUI "Enable imgui" ON)

# 4 is a good value, but it is easier to debug with 1
set(FRAST_WRITER_THREADS 4 CACHE INTEGER "Number of parallel jobs for frastFlatWriter" )
add_definitions(-DFRAST_WRITER_THREADS=${FRAST_WRITER_THREADS})

include(FetchContent)

#####################
# Catch2
#####################

if (TESTS)
	FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG        v3.2.1)
	FetchContent_MakeAvailable(Catch2)

	list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib)
	list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
	include(CTest)
	include(Catch)
endif()


#####################
# Fmt
#####################

set(FMT_TEST OFF)
set(CMAKE_CXX_FLAGS_OLD CMAKE_CXX_FLAGS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
FetchContent_Declare(
	dep_fmt
	GIT_REPOSITORY "https://github.com/fmtlib/fmt"
	GIT_TAG 8.1.0
	GIT_SHALLOW 1
)
FetchContent_MakeAvailable(dep_fmt)



include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/frast2) # Actually: don't do this, makes installing easier
if (EXISTS /usr/local/include/eigen3)
	include_directories(/usr/local/include/eigen3)
else()
	include_directories(/usr/include/eigen3)
endif()

set(libsGdal gdal)

find_package(Eigen3 REQUIRED)

if (STATIC_OPENCV)
	find_package(OpenCV REQUIRED NO_DEFAULT_PATH HINTS /opt/opencvStatic47)
else()
	if (EXISTS /usr/local/terrapixel/include/opencv4)
		find_package(OpenCV REQUIRED PATHS /usr/local/terrapixel/ NO_DEFAULT_PATH COMPONENTS opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui)
		include_directories(${OpenCV_INCLUDE_DIRS})
	else()
		find_package(OpenCV REQUIRED)
	endif()
endif()

include_directories(${OpenCV_INCLUDE_DIRS})
set(libsCv ${OpenCV_LIBRARIES})
message(STATUS "libsCv: ${libsCv}")

#####################
# Frast
#####################


add_library(frast2
	STATIC
	frast2/detail/bptree.cc
	frast2/detail/env.cc
	frast2/tpool/tpool.cc

	frast2/flat/flat_env.cc
	frast2/flat/writer.cc
	frast2/flat/writer_gdal.cc
	frast2/flat/writer_addo.cc
	frast2/flat/reader.cc
	frast2/flat/codec.cc
	)
# target_link_libraries(frast2 fmt::fmt pthread)
target_link_libraries(frast2 fmt::fmt pthread ${libsCv} ${libsZ})
# target_link_libraries(frast2 PUBLIC fmt::fmt pthread -Wl,--no-as-needed opencv_core -Wl,--as-needed)
# message(STATUS "opencv libs ${libsCv}")
# target_link_options(frast2 PUBLIC "-Wl,--whole-archive ${libsCv} ${libsZ} -Wl,--no-whole-archive")
# target_link_options(frast2 PRIVATE -Wl,--no-as-needed ${libsCv} ${libsZ} -Wl,--as-needed)

# add_executable(frastFlatWriter frast2/convertFlatBlocked/run.cc)
# target_link_libraries(frastFlatWriter frast2 fmt::fmt)
add_executable(frastFlatWriter frast2/flat/convertMain.cc)
target_link_libraries(frastFlatWriter frast2 fmt::fmt ${libsGdal})

add_executable(frastTool frast2/tool/main.cc)
target_link_libraries(frastTool frast2 fmt::fmt)

#####################
# FrastPy
#####################

find_package(pybind11 REQUIRED)
pybind11_add_module(frastpy2 frast2/py/frastpy2.cc)
target_link_libraries(frastpy2 PUBLIC frast2)

# Install libs, headers, and python files.

install(TARGETS frast2        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS frastFlatWriter LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(TARGETS frastTool     LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(DIRECTORY
	${CMAKE_CURRENT_SOURCE_DIR}/frast2/
	DESTINATION ${CMAKE_INSTALL_PREFIX}/include/frast2
	FILES_MATCHING
		PATTERN "*.h"
		PATTERN "*.hpp"
		PATTERN "*/__pycache__/*" EXCLUDE
		PATTERN "*/frastgl/*" EXCLUDE # this is covered in frastgl/CMakeLists.txt
		)

# Don't install to normal dist-packages because we cannot control that well.
# Instead, install to the user-configurable prefix in the "python" dir.
#EXECUTE_PROCESS( COMMAND "python -c 'import site; print(site.getsitepackages()[0])'" OUTPUT_VARIABLE PYTHON3_DIST_PACKAGE_DIR )

# For example, if DESTDIR=/data/temp, and CMAKE_INSTALL_PREFIX=/usr/local/:
#     PYTHONPATH=/data/temp/usr/local/python/ python3
# The above command will be able to load frastpy2
install(TARGETS frastpy2 LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/python)
install(DIRECTORY frast2/py/
	DESTINATION ${CMAKE_INSTALL_PREFIX}/python
	FILES_MATCHING
		PATTERN "*.py"
		PATTERN "*.proto"
		PATTERN "*/__pycache__" EXCLUDE)



#####################
# FrastGL
#####################

add_subdirectory(frast2/frastgl)

#####################
# Tests
#####################

if (TESTS)
	add_executable(testFlatWriter1 frast2/flat/test.cc)
	target_link_libraries(testFlatWriter1 Catch2::Catch2WithMain frast2 fmt::fmt)
	catch_discover_tests(testFlatWriter1)

	add_executable(testGdalStuff frast2/flat/testGdalStuff.cc)
	target_link_libraries(testGdalStuff Catch2::Catch2WithMain frast2 fmt::fmt ${libsGdal})
	catch_discover_tests(testGdalStuff)

	add_executable(testDataStructures frast2/detail/testDataStructures.cc)
	target_link_libraries(testDataStructures Catch2::Catch2WithMain frast2 fmt::fmt)
	catch_discover_tests(testDataStructures)

	add_executable(testLruCacheAndRingBuffer frast2/detail/testLruCacheAndRingBuffer.cc)
	target_link_libraries(testLruCacheAndRingBuffer Catch2::Catch2WithMain frast2 fmt::fmt)
	catch_discover_tests(testLruCacheAndRingBuffer)

	add_executable(ebpfStuff frast2/experiment/ebpf_stuff.cc)
	target_link_libraries(ebpfStuff Catch2::Catch2WithMain fmt::fmt)

	add_executable(testTpool frast2/tpool/test_tpool.cc)
	target_link_libraries(testTpool Catch2::Catch2WithMain frast2 fmt::fmt)
	catch_discover_tests(testTpool)
endif()

#####################
# Benchmarks
#####################

add_executable(benchmarkIteration frast2/detail/benchmarkIteration.cc)
target_link_libraries(benchmarkIteration frast2 fmt::fmt ${libsGdal})
