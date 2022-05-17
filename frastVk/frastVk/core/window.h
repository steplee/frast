#pragma once

#include <unistd.h>
#include <cstdio>

// #define GLFW_INCLUDE_GLU
#include <GLFW/glfw3.h>

#include <vector>
// #include <memory>
// #include <mutex>

// Callbacks should return false to signal that the event should propagate.
// If it returns true, the event will not propagate
struct UsesIO {
	inline virtual bool handleKey(int key, int scancode, int action, int mods) { return false; }
	inline virtual bool handleMousePress(int button, int action, int mods) { return false; }
	inline virtual bool handleMouseMotion(double x, double y) { return false; }
	//inline virtual void handleMouseNotify(int x, int y, bool isEntering) {}
};

class Window : public UsesIO {
	public:

		Window(int h, int w, bool headless);
		Window();
		~Window();
		virtual void destroyWindow();
		bool pollWindowEvents();
		std::vector<UsesIO*> ioUsers;
		std::string title;

		void setupWindow();

		inline virtual bool handleKey(int key, int scancode, int action, int mods) override { return false; }
		inline virtual bool handleMousePress(int button, int action, int mods) override { return false; }
		inline virtual bool handleMouseMotion(double x, double y) override { return false; }
		//inline virtual void handleMouseNotify(int x, int y, bool isEntering) {}

		uint32_t windowHeight, windowWidth;
		bool headless = false;
		GLFWwindow* glfwWindow = nullptr;

		std::vector<std::string> getWindowExtensions();

	private:


		/*
		void handleKey_(uint8_t key, uint8_t mod, bool isDown);
		void handleMousePress_(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing);
		void handleMouseMotion_(int x, int y, uint8_t mod);
		//void handleMouseNotify_(int x, int y, bool isEntering);
		*/
		static void _reshapeFunc(GLFWwindow* window, int w, int h);
		static void _keyboardFunc(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void _clickFunc(GLFWwindow* window, int button, int action, int mods);
		static void _motionFunc(GLFWwindow* window, double xpos, double ypos);
};
