#include "Window.h"

Window::Window()
{
	width = 800;
	height = 600;
	xChange = 0.0f;
	yChange = 0.0f;
	for (size_t i = 0; i < 1024; i++)
	{
		keys[i] = 0;
	}
}

Window::Window(GLfloat windowWidth, GLfloat windowHeight)/* : Window()*/
{
	width = windowWidth;
	height = windowHeight;
	xChange = 0.0f;
	yChange = 0.0f;
	for (size_t i = 0; i < 1024; i++)
	{
		keys[i] = 0;
	}
}

Window::~Window()
{
	glfwDestroyWindow(mainWindow);
	glfwTerminate();
}

int Window::Initialise()
{
	// initialise GLFW
	if (!glfwInit())
	{
		printf("GLFW INIT FAILED!");
		glfwTerminate();
		return 1;
	}

	// set up GLFW window properties
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	// stop backwards compatibility to assure no deprecation
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	// forward compatible
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	// Create a window
	mainWindow = glfwCreateWindow(width, height, "Super Game Engine", NULL, NULL);

	if (!mainWindow) {
		printf("GLFW Window Creation failed!");
		glfwTerminate();
		return 1;
	}

	// Get buffer size info(whats on the window is the buffer)
	glfwGetFramebufferSize(mainWindow, &bufferWidth, &bufferHeight);

	// ------- Basically done with GLFW initialisation ------
	// ---------- Now it is time to initialise glew ---------


	// set GLEW context to my only window
	glfwMakeContextCurrent(mainWindow);

	// handle key and mouse input
	createCallbacks();

	// lock the cursor
	glfwSetInputMode(mainWindow,GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// allow experimental features
	glewExperimental = GL_TRUE;

	// initialise glew
	if (glewInit() != GLEW_OK)
	{
		printf("GLEW initialisation failed!");
		glfwDestroyWindow(mainWindow);
		glfwTerminate();
		return 1;
	}

	// set viewport size
	glEnable(GL_DEPTH_TEST);

	glViewport(0, 0, bufferWidth, bufferHeight);

	// because of the static function, we have no way to know on which window the callback happens
	// this helps us get around that
	// this basically binds the glfw window to the pointer of my window object!
	glfwSetWindowUserPointer(mainWindow, this); // second parameter is any type of class
}

GLfloat Window::getXChange()
{
	GLfloat theChange = xChange;
	xChange = 0.0f;

	return theChange;
}

GLfloat Window::getYChange()
{
	GLfloat theChange = yChange;
	yChange = 0.0f;

	return theChange;
}

void Window::handleKeys(GLFWwindow* window, int key, int code, int action, int mode)
{
	// convert back the glfw pointer to normal window class pointer so that we can use the data
	Window* theWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	if (key >= 0 && key <= 1024)
	{
		if (action == GLFW_PRESS)
		{
			theWindow->keys[key] = true;
			printf("Key pressed:%d\n", key);
		}
		else if (action == GLFW_RELEASE)
		{
			theWindow->keys[key] = false;
			printf("Key released:%d\n", key);
		}
	}
}

void Window::handleMouse(GLFWwindow* window, double xPos, double yPos)
{
	Window* theWindow = static_cast<Window*>(glfwGetWindowUserPointer(window));

	// if its the very first movement
	if (theWindow->mouseFirstMoved)
	{
		theWindow->lastX = xPos;
		theWindow->lastY = yPos;
		theWindow->mouseFirstMoved = false;
	}

	theWindow->xChange = xPos - theWindow->lastX;
	// try to avoid inverted Y movement
	theWindow->yChange = theWindow->lastY - yPos;

	theWindow->lastX = xPos;
	theWindow->lastY = yPos;

	// this is how powerful the movement is
	//printf("x:%.6f, y:%.6f\n", theWindow->xChange, theWindow->yChange);
}

void Window::createCallbacks()
{
	glfwSetKeyCallback(mainWindow, handleKeys);
	glfwSetCursorPosCallback(mainWindow, handleMouse);
}