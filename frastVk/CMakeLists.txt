cmake_minimum_required(VERSION 3.18)

project(frastVk)

set(CMAKE_CXX_STANDARD 17)


include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/frastVk)
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
	frastVk/core/app.cc
	frastVk/core/buffer_utils.cc
	frastVk/core/render_state.cc

	# frastVk/clipmap1/clipmap1.cc
	# frastVk/clipmap1/clipmap1_load.cc

	# frastVk/tiled_renderer/tiled_renderer.cc
	frastVk/tiled_renderer2/tiled_renderer.cc

	frastVk/rt/rt.cc
	frastVk/rt/protos/rocktree.pb.cc

	frastVk/extra/particleCloud/particleCloud.cc
	frastVk/extra/frustum/frustum.cc
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


add_executable(runGlobe frastVk/tiled_renderer2/runGlobe.cc)
target_link_libraries(runGlobe frastVk)
target_precompile_headers(runGlobe REUSE_FROM frastVk)


add_executable(testParseRt frastVk/rt/parse.cc frastVk/rt/protos/rocktree.pb.cc)
target_link_libraries(testParseRt frastVk)
target_precompile_headers(testParseRt REUSE_FROM frastVk)

add_executable(runRt frastVk/rt/run.cc frastVk/rt/protos/rocktree.pb.cc)
target_link_libraries(runRt frastVk)
target_precompile_headers(runRt REUSE_FROM frastVk)


##################
#   Install
##################

install(TARGETS frastVk LIBRARY DESTINATION /usr/local/lib)
#install(FILES "frastVk/core/app.h" "frastVk/core/buffer_utils.h" "frastVk/core/render_state.h" "frastVk/core/window.hpp" DESTINATION /usr/local/include/frastVk/core)
#install(FILES "frastVk/rt/rt.h" "frastVk/rt/rt_decode.hpp" "frastVk/rt/protos/window.hpp" DESTINATION /usr/local/include/frastVk/core)
install(DIRECTORY frastVk/core frastVk/entity frastVk/extra frastVk/rt frastVk/tiled_renderer2 frastVk/utils DESTINATION /usr/local/include/frastVk FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY frastVk/shaders/compiled DESTINATION /usr/local/include/frastVk/shaders/ FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
