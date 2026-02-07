#pragma once

#include <stdio.h>

#include <GL/glew.h>

#include <GLFW/glfw3.h>

class Window
{
public:
	Window();
	Window(GLfloat windowWidth, GLfloat windowHeight);
	~Window();

	int Initialise();
	GLfloat getBufferWidth() { return bufferWidth; }
	GLfloat getBufferHeight() { return bufferHeight; }
	bool getShouldClose() { return glfwWindowShouldClose(mainWindow); }
	
	// not so object oriented but fuck it
	bool* getKeys() { return keys; };
	GLfloat getXChange();
	GLfloat getYChange();


	void swapBuffers() { glfwSwapBuffers(mainWindow); }
private:
	GLFWwindow* mainWindow;

	GLint width, height;
	GLint bufferWidth, bufferHeight;
	bool keys[1024];

	//mouse related variables
	GLfloat lastX, lastY;
	GLfloat xChange, yChange;
	bool mouseFirstMoved;

	// glfw callback needs the method to be static
	// 5 values cuz the callback has these variables
	// this happens cuz we use c++ and the function to get keys is c style
	// and we need to avoud the this pointer so we make it static
	static void handleKeys(GLFWwindow* window, int key, int code, int action, int mode); 
	static void handleMouse(GLFWwindow* window, double xPos, double yPos); 
	void createCallbacks();
};

