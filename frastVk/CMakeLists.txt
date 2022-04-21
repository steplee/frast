cmake_minimum_required(VERSION 3.18)

project(frastVk)

set(CMAKE_CXX_STANDARD 17)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/copiedHeaders/vulkanHeadersCopy)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/copiedHeaders/eigen3)

# Careful: with precompiled headers, definitions are finicky
add_definitions(-DVK_USE_PLATFORM_XCB_KHR=1)
#add_definitions(-DVULKAN_HPP_NO_CONSTRUCTORS=1)
#list(APPEND CMAKE_CXX_FLAGS " -g -O3 -fPIC ")
list(APPEND CMAKE_CXX_FLAGS " -g -O3 -fPIC -fopenmp ")

add_definitions(-DCOMPRESS_TERRAIN=1)
add_definitions(-DUSE_TURBOJPEG=1)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb xcb-keysyms)
#find_package(Vulkan REQUIRED)
set(VULKAN_LIBS vulkan)

option(VULKAN_DEBUG "debug layer" ON)
if (VULKAN_DEBUG)
	add_definitions(-DVULKAN_DEBUG=1)
	list(APPEND VULKAN_LIBS VkLayer_khronos_validation VkLayer_utils)
endif()

add_library(frastVk
	SHARED
	src/core/app.cc
	src/core/buffer_utils.cc
	src/core/render_state.cc

	src/clipmap1/clipmap1.cc
	src/clipmap1/clipmap1_load.cc

	src/tiled_renderer/tiled_renderer.cc
	src/tiled_renderer2/tiled_renderer.cc

	src/rt/rt.cc
	src/rt/protos/rocktree.pb.cc
)


target_link_libraries(frastVk
	PUBLIC
	frast
	# Why do I need these? They are statically linked to frast...
	lmdb turbojpeg z fmt


	${VULKAN_LIBS}
	dl
	${XCB_LIBRARIES}
	pthread
	stdc++fs
	protobuf
	)

# In a simple test, this results in over a 5x compile speedup.
target_precompile_headers(frastVk PUBLIC
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan_raii.hpp
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan.hpp
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan_enums.hpp
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan_handles.hpp
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan_structs.hpp
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan_hash.hpp
	copiedHeaders/vulkanHeadersCopy/vulkan/vulkan_funcs.hpp
	copiedHeaders/eigen3/Eigen/Core
	copiedHeaders/eigen3/Eigen/Dense
	copiedHeaders/eigen3/Eigen/Geometry
	copiedHeaders/eigen3/Eigen/StdVector
	)


add_executable(runGlobe src/tiled_renderer2/globe.cc)
target_link_libraries(runGlobe frastVk)
target_precompile_headers(runGlobe REUSE_FROM frastVk)


add_executable(testParseRt src/rt/parse.cc src/rt/protos/rocktree.pb.cc)
target_link_libraries(testParseRt frastVk)
target_precompile_headers(testParseRt REUSE_FROM frastVk)

add_executable(runRt src/rt/run.cc src/rt/protos/rocktree.pb.cc)
target_link_libraries(runRt frastVk)
target_precompile_headers(runRt REUSE_FROM frastVk)
