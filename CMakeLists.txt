cmake_minimum_required(VERSION 2.20)
project(frast)

set(CMAKE_CXX_FLAGS "-std=c++17 -gdwarf-2 -O0 -Werror=return-type")
# set(CMAKE_CXX_FLAGS_DEBUG)

#####################
# Catch2
#####################

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.2.1)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)



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



#####################
# Frast
#####################


add_library(frast2
	src2/db.cc
	src2/flat_writer.cc
	src2/detail/bptree.cc
	src2/detail/env.cc
	)
target_link_libraries(frast2 fmt::fmt)


#####################
# Tests
#####################

add_executable(testDataStructures src2/detail/testDataStructures.cc)
target_link_libraries(testDataStructures Catch2::Catch2WithMain frast2 fmt::fmt)
catch_discover_tests(testDataStructures)

add_executable(ebpfStuff src2/experiment/ebpf_stuff.cc)
target_link_libraries(ebpfStuff Catch2::Catch2WithMain fmt::fmt)
