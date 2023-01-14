

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

add_library(frastgl
	core/render_state.cc
	core/window.cc
	core/app.cc
	core/shader.cc
	extra/caster/castable.cc

	gt/ftr/ftr.cc
	gt/gt.cc
)

target_link_libraries(frastgl frast2 fmt::fmt ${libsCv} glfw GL GLEW)

add_executable(runFtr gt/ftr/runFtr.cc)
target_link_libraries(runFtr frastgl)
