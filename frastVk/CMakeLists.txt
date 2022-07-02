cmake_minimum_required(VERSION 3.18)

project(frastVk)

set (LOCAL_GLFW_BUILD_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/glfw/)
set (LOCAL_GLFW_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/glfw/include)
set (LOCAL_GLFW_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/glfw/lib)

# Build GLFW
include(ExternalProject)
ExternalProject_Add(glfw
	SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glfw

	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Starting GLFW cfg"
	WORKING_DIRECTORY <BINARY_DIR>
	COMMAND ${CMAKE_COMMAND} -E echo "Starting GLFW cfg"
	COMMAND ${CMAKE_COMMAND} <SOURCE_DIR> -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF -DBUILD_SHARED_LIBS=OFF


	BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Starting GLFW build"
	#WORKING_DIRECTORY <BINARY_DIR>
	COMMAND       ${CMAKE_COMMAND} --build <BINARY_DIR> -j 4
	STEP_TARGETS build

	# INSTALL_COMMAND ""
	INSTALL_COMMAND ${CMAKE_COMMAND} -E echo "install"
	COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --prefix ${LOCAL_GLFW_BUILD_PREFIX}
	)

link_directories(${LOCAL_GLFW_LIB_DIR})
include_directories(${LOCAL_GLFW_INCLUDE_DIR})

EXECUTE_PROCESS( COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE )
if (${ARCHITECTURE} STREQUAL "x86_64")
	set (IS_ARM FALSE)
else()
	set (IS_ARM TRUE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/frastVk)



# Careful: with precompiled headers, definitions are finicky
add_definitions(-DVK_USE_PLATFORM_XCB_KHR=1)

if (EXISTS "/opt/vulkanSdk")
	#set(CMAKE_PREFIX_PATH "/opt/vulkanSdk/share/cmake")
	set (VULKANSDK_ROOT "/opt/vulkanSdk")
	message(STATUS "Found vulkan sdk at /opt/vulkanSdk")
elseif (EXISTS "/data/vulkanSdk")
	#set(CMAKE_PREFIX_PATH "/data/vulkanSdk/share/cmake")
	set (VULKANSDK_ROOT "/data/vulkanSdk")
	message(INFO "Found vulkan sdk at /data/vulkanSdk")
endif()

if (DEFINED VULKANSDK_ROOT)
	set(VULKAN_HEADERS_DIR ${VULKANSDK_ROOT}/include)
	set(VULKAN_INC ${VULKAN_HEADERS_DIR}/vulkan)
	link_directories(${VULKANSDK_ROOT}/lib)
	message(STATUS "adding link dirs vulkan sdk at ${VULKANSDK_ROOT}/lib")
else()
	set(VULKAN_HEADERS_DIR /usr/local/include/)
	set(VULKAN_INC ${VULKAN_HEADERS_DIR}/vulkan)
endif()

if (IS_ARM)
	list(APPEND CMAKE_CXX_FLAGS " -g -O3 -fPIC -fopenmp ")
	link_directories(/usr/lib/aarch64-linux-gnu)
	set(EIGEN_INC /usr/local/include/eigen3)
else()
	list(APPEND CMAKE_CXX_FLAGS " -g -O3 -march=native -fPIC -fopenmp ")
endif()

# include_directories(${COPIED_HEADERS_DIR}/vulkanHeadersCopy)
# include_directories(${COPIED_HEADERS_DIR}/vulkan)
# include_directories(${COPIED_HEADERS_DIR}/eigen3)
set(EIGEN_INC /usr/local/include/eigen3)
include_directories(${VULKAN_HEADERS_DIR})
include_directories(${EIGEN_INC})

add_definitions(-DCOMPRESS_TERRAIN=1)
add_definitions(-DUSE_TURBOJPEG=1)

find_package(PkgConfig REQUIRED)
# pkg_check_modules(XCB REQUIRED xcb xcb-keysyms)
#find_package(Vulkan REQUIRED)
if (IS_ARM)
	set(VULKAN_LIBS vulkan)
else()
	set(VULKAN_LIBS vulkan)
	# set(VULKAN_LIBS /opt/vulkanSdk/lib/libvulkan.so.1.3.211)
endif()

option(VULKAN_DEBUG "debug layer" ON)
if (VULKAN_DEBUG)
	add_definitions(-DVULKAN_DEBUG=1)
	list(APPEND VULKAN_LIBS VkLayer_khronos_validation VkLayer_utils)
endif()

add_custom_command(
	OUTPUT ${PROJECT_SOURCE_DIR}/frastVk/gt/rt/protos/rocktree.pb.cc
  #COMMAND protoc --proto_path ../../../protos rocktree.proto --cpp_out=.
  COMMAND cp ${PROJECT_SOURCE_DIR}/protos/* .
  COMMAND protoc rocktree.proto --cpp_out=.
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/frastVk/gt/rt/protos)

set(IMGUI_SOURCES "")
option(USE_IMGUI "Use ImGUI" ON)
add_definitions(-DUSE_IMGUI=1)
if (USE_IMGUI)
  list(APPEND IMGUI_SOURCES
		frastVk/core/imgui_app.cc

	    frastVk/core/imgui/imgui.cpp
	    frastVk/core/imgui/imgui_demo.cpp
	    frastVk/core/imgui/imgui_draw.cpp
	    frastVk/core/imgui/imgui_tables.cpp
	    frastVk/core/imgui/imgui_widgets.cpp
	    frastVk/core/imgui/backends/imgui_impl_vulkan.cpp
	    frastVk/core/imgui/backends/imgui_impl_glfw.cpp
    )
endif()

add_library(frastVk
	SHARED
	#frastVk/core/app.cc
	#frastVk/core/buffer_utils.cc
	#frastVk/core/render_state.cc
	#frastVk/core/window.cc
	#frastVk/rt/rt.cc
	#frastVk/rt/protos/rocktree.pb.cc
	#frastVk/extra/particleCloud/particleCloud.cc
	#frastVk/extra/frustum/frustum.cc
	#frastVk/extra/text/textSet.cc
	#frastVk/extra/caster/castable.cc
	#frastVk/extra/primitives/earthEllipsoid.cc
	#frastVk/extra/primitives/ellipsoid.cc

	frastVk/core/fvkApi.cc
	frastVk/core/fvkApi_detail.cc
	frastVk/core/fvkShaders.cc
	frastVk/core/window.cc
	frastVk/core/render_state.cc

	frastVk/gt/gt.cc
	frastVk/gt/rt/rt.cc
	frastVk/gt/rt/protos/rocktree.pb.cc

	frastVk/gt/ftr/ftr.cc

	frastVk/extra/caster/castable.cc
	frastVk/extra/frustum/frustum.cc
	frastVk/extra/particleCloud/particleCloud.cc
	frastVk/extra/text/textSet.cc
	frastVk/extra/primitives/ellipsoid.cc
	frastVk/extra/primitives/earthEllipsoid.cc

    ${IMGUI_SOURCES}
)


target_link_libraries(frastVk
	PUBLIC
	frast
	# Why do I need these? They are statically linked to frast...
	lmdb turbojpeg z fmt


	${VULKAN_LIBS}
	dl
	# ${XCB_LIBRARIES}
	glfw3
	X11
	pthread
	stdc++fs
	protobuf
	)

# In a simple test, this results in over a 5x compile speedup.
target_precompile_headers(frastVk PUBLIC
	${VULKAN_INC}/vk_platform.h
	${VULKAN_INC}/vulkan_core.h
	${VULKAN_INC}/vulkan.h
	#${VULKAN_INC}/vulkan_raii.hpp
	#${VULKAN_INC}/vulkan.hpp
	#${VULKAN_INC}/vulkan_enums.hpp
	#${VULKAN_INC}/vulkan_handles.hpp
	#${VULKAN_INC}/vulkan_structs.hpp
	#${VULKAN_INC}/vulkan_hash.hpp
	#${VULKAN_INC}/vulkan_funcs.hpp
	${EIGEN_INC}/Eigen/Core
	${EIGEN_INC}/Eigen/Dense
	${EIGEN_INC}/Eigen/Geometry
	${EIGEN_INC}/Eigen/StdVector
	)


add_executable(fvkTestApp frastVk/core/fvkTestApp.cc)
target_link_libraries(fvkTestApp frastVk)

add_executable(fvkRunRt frastVk/gt/rt/runRtTest.cc)
target_link_libraries(fvkRunRt frastVk)

add_executable(fvkRunFtr frastVk/gt/ftr/runFtrTest.cc)
target_link_libraries(fvkRunFtr frastVk)

#[[
add_executable(testParseRt frastVk/rt/parse.cc frastVk/rt/protos/rocktree.pb.cc)
target_link_libraries(testParseRt frastVk)
target_precompile_headers(testParseRt REUSE_FROM frastVk)

add_executable(runRt frastVk/rt/run.cc frastVk/rt/protos/rocktree.pb.cc)
target_link_libraries(runRt frastVk)
target_precompile_headers(runRt REUSE_FROM frastVk)

add_executable(rtGenSets frastVk/rt/rtRenderSets.cc frastVk/rt/protos/rocktree.pb.cc)
target_link_libraries(rtGenSets frastVk)
target_precompile_headers(rtGenSets REUSE_FROM frastVk)
#]]

##################
#   Install
##################

install(TARGETS frastVk LIBRARY DESTINATION /usr/local/lib)
install(DIRECTORY frastVk/core frastVk/entity frastVk/extra frastVk/gt frastVk/utils DESTINATION /usr/local/include/frastVk FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY frastVk/shaders/compiled DESTINATION /usr/local/include/frastVk/shaders/ FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# Also get the GLFW headers
install(FILES ${LOCAL_GLFW_INCLUDE_DIR}/GLFW/glfw3.h ${LOCAL_GLFW_INCLUDE_DIR}/GLFW/glfw3native.h DESTINATION /usr/local/include/frastVk/GLFW)


