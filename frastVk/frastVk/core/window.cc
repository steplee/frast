#include "window.h"
#include <iostream>
#include <fmt/core.h>
#include <mutex>

static bool __didInit = false;
static int windowCnt = 0;
static std::mutex __window_mtx;
// extern std::mutex __window_mtx;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

static void __doInit() {
}

void MyGlfwWindow::setupWindow() {

	__window_mtx.lock();
    if (!__didInit) {
		glfwSetErrorCallback(glfw_error_callback);
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW." << std::endl;
            glfwTerminate();
        }
	}
	windowCnt++;
	__didInit = true;
	__window_mtx.unlock();

	bool egl = false;
    if (egl) glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    // if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindow = glfwCreateWindow(windowWidth, windowHeight, title.c_str(), NULL, NULL);
    if (glfwWindow == NULL) {
        std::cerr << "Failed to open GLFW window: " << title << " " << windowWidth << "," << windowHeight << std::endl;
        glfwTerminate();
    }

    // glfwMakeContextCurrent(glfwWindow);
	int ww, hh;
    glfwGetWindowSize(glfwWindow, &ww, &hh);
	windowWidth = ww;
	windowHeight = hh;

    // Callback
    glfwSetWindowSizeCallback(glfwWindow, &_reshapeFunc);
    glfwSetKeyCallback(glfwWindow, &_keyboardFunc);
    glfwSetMouseButtonCallback(glfwWindow, &_clickFunc);
    glfwSetCursorPosCallback(glfwWindow, &_motionFunc);

    glfwSetWindowUserPointer(glfwWindow, reinterpret_cast<void *>(this));
}

MyGlfwWindow::MyGlfwWindow(int w, int h) : Window(w,h) {
	title = "noName";
}
MyGlfwWindow::MyGlfwWindow() : Window() {
	title = "noName";
}

MyGlfwWindow::~MyGlfwWindow() {
	destroyWindow();
}
void MyGlfwWindow::destroyWindow() {
	ioUsers.clear();
    if (glfwWindow) glfwDestroyWindow(glfwWindow);
    glfwWindow = nullptr;
}
bool MyGlfwWindow::pollWindowEvents() {
    glfwPollEvents();
	// fmt::print(" - Polling events.\n");
	return false;
}

void MyGlfwWindow::_reshapeFunc(GLFWwindow* glfwWindow, int w, int h) {
    MyGlfwWindow* theWindow = reinterpret_cast<MyGlfwWindow *>(glfwGetWindowUserPointer(glfwWindow));
}
void MyGlfwWindow::_keyboardFunc(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) {
    MyGlfwWindow* theWindow = reinterpret_cast<MyGlfwWindow *>(glfwGetWindowUserPointer(glfwWindow));
	bool stop = theWindow->handleKey(key, scancode, action, mods);
	for (auto l : theWindow->ioUsers) {
		if (stop) break;
		stop = l->handleKey(key, scancode, action, mods);
	}
}
void MyGlfwWindow::_clickFunc(GLFWwindow* glfwWindow, int button, int action, int mods) {
    MyGlfwWindow* theWindow = reinterpret_cast<MyGlfwWindow *>(glfwGetWindowUserPointer(glfwWindow));
	bool stop = theWindow->handleMousePress(button, action, mods);
	for (auto l : theWindow->ioUsers) {
		if (stop) break;
		stop = l->handleMousePress(button, action, mods);
	}
}
void MyGlfwWindow::_motionFunc(GLFWwindow* glfwWindow, double xpos, double ypos) {
    MyGlfwWindow* theWindow = reinterpret_cast<MyGlfwWindow *>(glfwGetWindowUserPointer(glfwWindow));
	bool stop = theWindow->handleMouseMotion(xpos,ypos);
	for (auto l : theWindow->ioUsers) {
		if (stop) break;
		stop = l->handleMouseMotion(xpos,ypos);
	}
}

std::vector<std::string> MyGlfwWindow::getWindowExtensions() {

	__window_mtx.lock();
    if (!__didInit) {
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW." << std::endl;
            glfwTerminate();
        } else { __didInit = true; }
	}
	__window_mtx.unlock();

	std::vector<std::string> out;
	uint32_t count;
	char** extensions = (char**)glfwGetRequiredInstanceExtensions(&count);
	for (int i=0; i<count; i++) out.push_back(std::string{extensions[i]});
	return out;
}






MyHeadlessWindow::MyHeadlessWindow(int w, int h) : Window(w,h) {
}
std::vector<std::string> MyHeadlessWindow::getWindowExtensions() {
	return { "VK_KHR_surface" };
}
void MyHeadlessWindow::setupWindow() {
}
MyHeadlessWindow::~MyHeadlessWindow() {
}
