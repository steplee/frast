#pragma once

#include <unistd.h>
#include <cstdio>
#include <string>

// #define GLFW_INCLUDE_GLU
// NOTE: This file should be copied to this exact dir from the CMakeLists.txt after making glfw3 available
#include <GL/glew.h>
#include <GL/gl.h>
#include "./glfw3.h"

#include <vector>
// #include <memory>
// #include <mutex>

namespace frast {

// Callbacks should return false to signal that the event should propagate.
// If it returns true, the event will not propagate
struct UsesIO {
	inline virtual bool handleKey(int key, int scancode, int action, int mods) { return false; }
	inline virtual bool handleMousePress(int button, int action, int mods) { return false; }
	inline virtual bool handleMouseMotion(double x, double y) { return false; }
	//inline virtual void handleMouseNotify(int x, int y, bool isEntering) {}
};

class Window {
	public:
		virtual void beginFrame() =0;
		virtual void endFrame() =0;

		virtual void setupWindow() =0;
		virtual bool pollWindowEvents() =0;
		virtual void addIoUser(UsesIO* ptr) =0;

		inline Window(uint32_t w, uint32_t h) : windowWidth(w), windowHeight(h) {};
		inline Window() : windowWidth(0), windowHeight(0) {};
		inline virtual ~Window() {}

		virtual bool headless() =0;

	public:
		uint32_t windowHeight, windowWidth;

};

class MyGlfwWindow : public Window, public UsesIO {
	public:

		MyGlfwWindow(int w, int h, bool headless);
		MyGlfwWindow();
		virtual ~MyGlfwWindow();
		void destroyWindow();
		virtual bool pollWindowEvents() override;
		std::string title;

		virtual void beginFrame() override;
		virtual void endFrame() override;

		virtual void setupWindow() override;

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override { return false; }
		inline virtual bool handleMousePress(int button, int action, int mods) override { return false; }
		inline virtual bool handleMouseMotion(double x, double y) override { return false; }
		//inline virtual void handleMouseNotify(int x, int y, bool isEntering) {}

		GLFWwindow* glfwWindow = nullptr;

		inline virtual bool headless() override { return false; }
		inline virtual void addIoUser(UsesIO* ptr) override { ioUsers.push_back(ptr); }


	private:

		std::vector<UsesIO*> ioUsers;
		static void _reshapeFunc(GLFWwindow* window, int w, int h);
		static void _keyboardFunc(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void _clickFunc(GLFWwindow* window, int button, int action, int mods);
		static void _motionFunc(GLFWwindow* window, double xpos, double ypos);
};

class MyHeadlessWindow : public Window {
	public:
		MyHeadlessWindow(int w, int h);
		virtual ~MyHeadlessWindow();

		virtual void setupWindow() override;
		inline virtual bool pollWindowEvents() override { return false; };

		inline virtual bool headless() override { return true; }
		inline virtual void addIoUser(UsesIO* ptr) override { }

};

}
