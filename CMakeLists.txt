cmake_minimum_required(VERSION 3.18)

project(frast)

##################
#   Options
##################

option(DEBUG "debug mode" OFF)
option(DEBUG_SYMBOLS "include -g" ON)
option(TIMING "time certain operations, print on exit" ON)
option(USE_TURBOJPEG "use turbojpeg directly, instead of opencv" ON)

set(ADDO_THREADS 8 CACHE INT "threads used")
set(CONVERT_THREADS 8 CACHE INT "threads used")
set(WRITER_NBUF 32 CACHE INT "buffers allocated per thread")
set(TURBOJPEG_QUALITY 92 CACHE INT "jpeg quality, must be [0-100]")

##################
#   Setup Deps
##################

# TODO: I am in the middle of completely phasing out OpenCV (except for DEBUG_RASTERIO)
find_package(OpenCV REQUIRED)

find_package(Fmt REQUIRED)
find_package(GDAL REQUIRED)
include_directories(/usr/local/include/eigen3)
include_directories(/usr/include/gdal)
set(COMMON_LIBS pthread lmdb fmt::fmt ${OpenCV_LIBRARIES} ${GDAL_LIBRARIES})

if (USE_TURBOJPEG)
	list(APPEND COMMON_LIBS turbojpeg)
endif()

find_package(pybind11 REQUIRED)

##################
#   Setup Global Flags
##################

set(CMAKE_CXX_STANDARD 17)

set(cxx_opts " -march=native -O3 -fopenmp -Werror=return-type ")
set(includes "")
if (DEBUG_SYMBOLS)
	set(cxx_opts "${cxx_opts} -g ")
endif()
if (DEBUG)
	add_definitions(-DDEBUG_RASTERIO=1 -DDEBUG_PRINT=1)
	list(APPEND includes ${OpenCV_INCLUDE_DIRS})
endif()
if (TIMING)
	add_definitions(-DUSE_TIMER=1)
endif()
set(CMAKE_CXX_FLAGS ${cxx_opts})


##################
#   Main Lib
##################

add_library(frast
	src/image.cc
	#src/image2.cc # Shockingly slow.
	src/db.cc)

set_target_properties(frast PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(frast INTERFACE ${COMMON_LIBS})
target_include_directories(frast PUBLIC ${includes})
if (USE_TURBOJPEG)
	target_compile_definitions(frast PUBLIC USE_TURBOJPEG TURBOJPEG_QUALITY=${TURBOJPEG_QUALITY})
endif()

##################
#   Apps
##################

add_executable(frastAddo src/frastAddo.cc)
add_executable(frastConvertGdal src/frastConvertGdal.cc)
add_executable(frastDump src/frastDump.cc)
add_executable(frastInfo src/frastInfo.cc)
add_executable(frastMerge src/frastMerge.cc)

target_link_libraries(frastAddo PUBLIC frast)
target_link_libraries(frastConvertGdal PUBLIC frast)
target_link_libraries(frastDump PUBLIC frast)
target_link_libraries(frastInfo PUBLIC frast)
target_link_libraries(frastMerge PUBLIC frast)

target_compile_definitions(frastAddo PUBLIC ADDO_THREADS=${ADDO_THREADS} WRITER_NBUF=${WRITER_NBUF})
target_compile_definitions(frastConvertGdal PUBLIC CONVERT_THREADS=${CONVERT_THREADS} WRITER_NBUF=${WRITER_NBUF})

##################
#   Python Lib
##################

pybind11_add_module(frastpy src/frastPy.cc)
target_link_libraries(frastpy PUBLIC frast)

##################
#   Install
##################

install(TARGETS frast LIBRARY DESTINATION /usr/local/lib)
install(FILES "src/db.h" "src/image.h" DESTINATION /usr/local/include/frast/)
install(FILES
	"src/utils/common.h"
	"src/utils/data_structures.hpp"
	"src/utils/solve.hpp"
	"src/utils/timer.hpp"
	DESTINATION /usr/local/include/frast/utils)
install(TARGETS frastAddo RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastConvertGdal RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastDump RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastInfo RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastMerge RUNTIME DESTINATION /usr/local/bin)

execute_process(COMMAND python3 -c "import site;print([p for p in site.getsitepackages() if p.endswith(('site-packages', 'dist-packages')) ][0])" OUTPUT_VARIABLE py_install_dir)
string(STRIP "${py_install_dir}" py_install_dir)
message(STATUS "python3 install dir ${py_install_dir}")
install(TARGETS frastpy DESTINATION ${py_install_dir}/)
