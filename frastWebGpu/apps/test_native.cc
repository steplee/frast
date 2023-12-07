#include "../webgpu_helpers.h"

#include <glfw.h>

int main() {

	if (!glfwInit()) {
		std::cerr << "Could not initialize GLFW!" << std::endl;
		return 1;
	}

	GLFWwindow* window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);

	if (!window) {
		std::cerr << "Could not open window!" << std::endl;
		glfwTerminate();
		return 1;
	}

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}

	glfwDestroyWindow(window);

	glfwTerminate();

	return 0;
}
