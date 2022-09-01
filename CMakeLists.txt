cmake_minimum_required(VERSION 3.16)

project(frast)

EXECUTE_PROCESS( COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE )
if (${ARCHITECTURE} STREQUAL "x86_64")
	set (IS_ARM FALSE)
else()
	set (IS_ARM TRUE)
endif()

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsized-deallocation")
#add_compile_options("-stdlib=libc++" "-lc++abi")

##################
#   Options
##################

option(DEBUG "debug mode" OFF)
option(DEBUG_SYMBOLS "include -g" OFF)
option(TIMING "time certain operations, print on exit" ON)
option(USE_TURBOJPEG "use turbojpeg directly, instead of opencv" ON)
option(TESTS "build tests" OFF)

set(ADDO_THREADS 8 CACHE INT "threads used")
set(CONVERT_THREADS 8 CACHE INT "threads used")
set(WRITER_NBUF 32 CACHE INT "buffers allocated per thread")
set(TURBOJPEG_QUALITY 92 CACHE INT "jpeg quality, must be [0-100]")
set(COMPRESS_TERRAIN 1 CACHE INT "use zlib to compress terrain")


##################
#   Setup Deps
##################

#find_package(OpenCV REQUIRED)
set(includes "")

find_package(Fmt REQUIRED)
find_package(GDAL REQUIRED)
if (EXISTS /usr/local/include/eigen3/)
	include_directories(/usr/local/include/eigen3)
else()
	include_directories(/usr/include/eigen3)
endif()
include_directories(/usr/include/gdal)
set(COMMON_LIBS pthread lmdb fmt::fmt)

if (USE_TURBOJPEG)
	list(APPEND COMMON_LIBS turbojpeg)
endif()
if (COMPRESS_TERRAIN)
	add_definitions(-DCOMPRESS_TERRAIN=1)
	list(APPEND COMMON_LIBS z)
endif()

find_package(pybind11 REQUIRED)

##################
#   Setup Global Flags
##################

set(CMAKE_CXX_STANDARD 17)

if (IS_ARM)
	set(cxx_opts "  -O3 -fopenmp -Werror=return-type ")
	link_directories(/usr/lib/aarch64-linux-gnu)
else()
	# set(cxx_opts "  -O3 -fopenmp -march=native -Werror=return-type -Wl,--as-needed -Wl,--unresolved-symbols=ignore-in-shared-libs")
	set(cxx_opts "  -O3 -fopenmp -march=native -Werror=return-type")
endif()
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


set(X_LIBS xcb xcb-image xcb-keysyms xcb-util X11)

##################
#   Main Lib
##################

add_library(frast
	src/image.cc
	#src/image2.cc # Shockingly slow.
	src/db.cc
	src/utils/displayImage.cc
	)

set_target_properties(frast PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(frast INTERFACE ${COMMON_LIBS})
target_include_directories(frast PUBLIC ${includes})
if (USE_TURBOJPEG)
	target_compile_definitions(frast PUBLIC USE_TURBOJPEG TURBOJPEG_QUALITY=${TURBOJPEG_QUALITY})
endif()

##################
#   Apps
##################

add_executable(frastAddo src/apps/frastAddo.cc)
add_executable(frastConvertGdal src/apps/frastConvertGdal.cc)
add_executable(frastConvertGdalMany src/apps/frastConvertGdalMany.cc)
add_executable(frastDump src/apps/frastDump.cc)
add_executable(frastInfo src/apps/frastInfo.cc)
add_executable(frastMerge src/apps/frastMerge.cc)

target_link_libraries(frastAddo PUBLIC frast)
# target_link_libraries(frastConvertGdal PUBLIC frast -Wl,--as-needed ${GDAL_LIBRARIES} ${X_LIBS})
target_link_libraries(frastConvertGdal PUBLIC frast -Wl,--as-needed ${GDAL_LIBRARIES})
target_link_libraries(frastConvertGdalMany PUBLIC frast -Wl,--as-needed ${GDAL_LIBRARIES})
target_link_libraries(frastDump PUBLIC frast)
target_link_libraries(frastInfo PUBLIC frast)
target_link_libraries(frastMerge PUBLIC frast)

target_compile_definitions(frastAddo PUBLIC ADDO_THREADS=${ADDO_THREADS} WRITER_NBUF=${WRITER_NBUF})
target_compile_definitions(frastConvertGdal PUBLIC CONVERT_THREADS=${CONVERT_THREADS} WRITER_NBUF=${WRITER_NBUF})
target_compile_definitions(frastConvertGdalMany PUBLIC CONVERT_THREADS=${CONVERT_THREADS} WRITER_NBUF=${WRITER_NBUF})

##################
#   Python Lib
##################

pybind11_add_module(frastpy src/frastPy.cc)
target_link_libraries(frastpy PUBLIC frast)


##################
#   Tests
##################

if (TESTS)
	include(FetchContent)
	FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG release-1.12.1
	)
	FetchContent_MakeAvailable(googletest)
	include(GoogleTest)
	enable_testing()

	add_executable(test_db src/tests/db.cc)
	target_link_libraries(test_db frast GTest::gtest_main)
	gtest_discover_tests(test_db)

	add_executable(test_utils src/tests/utils.cc)
	target_link_libraries(test_utils frast GTest::gtest_main)
	gtest_discover_tests(test_utils)

	add_executable(test_gdalConversion src/tests/gdalConversion.cc)
	target_link_libraries(test_gdalConversion frast GTest::gtest_main)
	gtest_discover_tests(test_gdalConversion)
	target_compile_definitions(test_gdalConversion PUBLIC CONVERT_THREADS=${CONVERT_THREADS} WRITER_NBUF=${WRITER_NBUF})
endif()

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
	"src/utils/displayImage.h"
	DESTINATION /usr/local/include/frast/utils)
install(TARGETS frastAddo RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastConvertGdal RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastConvertGdalMany RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastDump RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastInfo RUNTIME DESTINATION /usr/local/bin)
install(TARGETS frastMerge RUNTIME DESTINATION /usr/local/bin)

execute_process(COMMAND python3 -c "import site;print([p for p in site.getsitepackages() if p.endswith(('site-packages', 'dist-packages')) ][0])" OUTPUT_VARIABLE py_install_dir)
string(STRIP "${py_install_dir}" py_install_dir)
message(STATUS "python3 install dir ${py_install_dir}")
install(TARGETS frastpy DESTINATION ${py_install_dir}/)
