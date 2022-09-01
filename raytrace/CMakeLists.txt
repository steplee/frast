cmake_minimum_required(VERSION 3.16)

project(frastRt LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# set(CMAKE_CXX_FLAGS_DEBUG "-g -O1")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

if (EXISTS /usr/local/include/eigen3/)
	set(EIGEN_INC /usr/local/include/eigen3)
else()
	set(EIGEN_INC /usr/include/eigen3)
endif()
include_directories(${EIGEN_INC})
include_directories(/usr/local/cuda/include/)

find_package(PkgConfig REQUIRED)

add_executable(rt src/main.cc src/trace_cpu.cc)
target_link_libraries(rt fmt)
