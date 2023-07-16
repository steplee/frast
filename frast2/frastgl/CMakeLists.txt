

# include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#####################
# Glfw 3
#####################

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_INSTALL OFF)
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw
)

FetchContent_MakeAvailable(glfw)

# NOTE: Copy a local version of the glf3 header.
file(COPY ${glfw_SOURCE_DIR}/include/GLFW/glfw3.h DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/core)


#####################
# ImGui
#####################

# Download git repo with FetchContent, then do not build: just copy the sources/headers into frastgl/core/imgui dir.
if (ENABLE_IMGUI)
	FetchContent_Declare(
		dep_imgui
		GIT_REPOSITORY https://github.com/ocornut/imgui.git
		GIT_TAG v1.89.2
		GIT_SHALLOW 1
	)
	FetchContent_MakeAvailable(dep_imgui)
	foreach(fi imconfig.h imgui.cpp imgui.h imgui_demo.cpp imgui_draw.cpp imgui_internal.h imgui_tables.cpp imgui_widgets.cpp imstb_rectpack.h imstb_textedit.h imstb_truetype.h backends/imgui_impl_glfw.h backends/imgui_impl_glfw.cpp backends/imgui_impl_opengl3.cpp backends/imgui_impl_opengl3.h backends/imgui_impl_opengl3_loader.h)
		# file(COPY ${dep_imgui_SOURCE_DIR}/${fi} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/frast2/frastgl/core/imgui)
		file(COPY ${dep_imgui_SOURCE_DIR}/${fi} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated)
	endforeach()

	set(imgui_sources
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui_demo.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui_draw.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui_tables.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui_widgets.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui_impl_glfw.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated/imgui_impl_opengl3.cpp)
	set(frast_imgui_sources
		${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/imgui_app.cc
		)

	# Implot.
	FetchContent_Declare(
		dep_implot
		GIT_REPOSITORY https://github.com/epezent/implot.git
		GIT_TAG cc5e1da
		GIT_SHALLOW 1
	)
	FetchContent_MakeAvailable(dep_implot)
	foreach(fi implot.cpp implot_demo.cpp implot.h implot_internal.h implot_items.cpp)
		file(COPY ${dep_implot_SOURCE_DIR}/${fi} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/core/implot/generated)
	endforeach()

	set(implot_sources
		${CMAKE_CURRENT_SOURCE_DIR}/core/implot/generated/implot.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/implot/generated/implot.h
		${CMAKE_CURRENT_SOURCE_DIR}/core/implot/generated/implot_internal.h
		${CMAKE_CURRENT_SOURCE_DIR}/core/implot/generated/implot_items.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/core/implot/generated/implot_demo.cpp)
	set(frast_implot_sources
		# ${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/imgui_app.cc
		)

else()
	set(imgui_sources "")
	set(frast_imgui_sources "")
	set(implot_sources "")
	set(frast_implot_sources "")
endif()





#####################
# FrastGl
#####################


add_library(frastgl
	STATIC
	core/render_state.cc
	core/cameras.cc
	core/window.cc
	core/app.cc
	core/shader.cc

	extra/caster/castable.cc
	extra/earth/earth.cc
	extra/frustum/frustum.cc
	extra/ellipsoid/ellipsoid.cc
	extra/textSet/textSet.cc
	extra/loaders/mesh.cc
	extra/loaders/obj.cc

	gt/gt.cc
	gt/ftr/ftr.cc
	gt/rt/rt.cc
	gt/rt/protos/rocktree.pb.cc

	${imgui_sources} ${frast_imgui_sources}
	${implot_sources} ${frast_implot_sources}
)

target_include_directories(frastgl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated)

# find_library(protobuf_lib libprotobuf.so PATHS /usr/lib64 REQUIREd)
target_link_libraries(frastgl frast2 fmt::fmt ${libsCv} glfw GL GLEW protobuf)
# target_link_libraries(frastgl frast2 fmt::fmt ${libsCv} glfw GL GLEW ${protobuf_lib})
# target_link_libraries(frastgl frast2 fmt::fmt ${libsCv} glfw GL GLEW /usr/lib/x86_64-linux-gnu/libprotobuf.so)

add_executable(runFtr gt/ftr/runFtr.cc)
target_link_libraries(runFtr frastgl)
add_executable(runRtr gt/rt/runRt.cc)
target_link_libraries(runRtr frastgl)

if (TESTS)
	add_executable(runPingPongTest1 extra/pingPong/test1.cc)
	target_link_libraries(runPingPongTest1 frastgl)
	add_executable(runPingPongTest2 extra/pingPong/test2.cc)
	target_link_libraries(runPingPongTest2 frastgl)

	if (ENABLE_IMGUI)
		add_executable(runImplotDemo core/implot/demo.cc)
		target_include_directories(runImplotDemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/imgui/generated)
		target_link_libraries(runImplotDemo frastgl)
	endif()
endif()


#####################
# Proto
#####################

add_custom_command(
  #TARGET frastgl
  #PRE_BUILD
  #BYPRODUCTS "${PROJECT_SOURCE_DIR}/frast2/frastgl/gt/rt/protos/rocktree.pb.cc"
  OUTPUT "${PROJECT_SOURCE_DIR}/frast2/frastgl/gt/rt/protos/rocktree.pb.cc"
  "${PROJECT_SOURCE_DIR}/frast2/frastgl/gt/rt/protos/rocktree.pb.h"
  COMMAND protoc --cpp_out=./ rocktree.proto
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/frast2/frastgl/gt/rt/protos")


#####################
# Install
#####################

# NOTE: This copies private headers too, but that is okay.
install(TARGETS frastgl LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS glfw LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(DIRECTORY
	# ${CMAKE_CURRENT_SOURCE_DIR}/core
	${CMAKE_CURRENT_SOURCE_DIR}/
	DESTINATION ${CMAKE_INSTALL_PREFIX}/include/frast2/frastgl
	FILES_MATCHING
		PATTERN "*.h"
		PATTERN "*.hpp"
		PATTERN "*shaders/*.h" EXCLUDE # Exclude shaders.
		)
