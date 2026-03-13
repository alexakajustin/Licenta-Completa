#include "InputHandler.h"
#include "Window.h"
#include "Camera.h"
#include "SceneManager.h"

#include "imgui.h"

InputHandler::InputHandler()
	: lastLMBState(false)
{
}

InputHandler::~InputHandler()
{
}

void InputHandler::UpdateCamera(Window& window, Camera& camera, GLfloat deltaTime)
{
	if (!window.isCursorEnabled())
	{
		// FPS camera mode
		camera.keyControl(window.getKeys(), deltaTime);
		camera.mouseControl(window.getXChange(), window.getYChange());
	}
}

void InputHandler::UpdateEditor(Window& window, Camera& camera, SceneManager& scene,
								const glm::mat4& projection)
{
	if (!window.isCursorEnabled())
	{
		lastLMBState = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];
		return;
	}

	// Editor mode — handle picking and gizmo dragging
	bool currentLMB = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];
	bool wantCapture = ImGui::GetIO().WantCaptureMouse;

	glm::mat4 view = camera.calculateViewMatrix();

	// Use screen coordinates for scaling math in HandleMousePress (SceneManager::PickObject handles pixel scaling)
	double mouseX, mouseY;
	glfwGetCursorPos(window.getWindow(), &mouseX, &mouseY);

	int winW, winH;
	glfwGetWindowSize(window.getWindow(), &winW, &winH);
	float screenW = (float)winW;
	float screenH = (float)winH;

	int fbw, fbh;
	glfwGetFramebufferSize(window.getWindow(), &fbw, &fbh);

	// Scale GLFW's logical cursor coordinates to raw pixel coordinates
	float pixelMouseX = (float)mouseX * ((float)fbw / screenW);
	float pixelMouseY = (float)mouseY * ((float)fbh / screenH);

	if (currentLMB && !lastLMBState && !wantCapture)
	{
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS,
			pixelMouseX, pixelMouseY, projection, view,
			camera.getCameraPosition(), (float)fbw, (float)fbh);
	}
	else if (!currentLMB && lastLMBState)
	{
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE,
			0, 0, projection, view,
			camera.getCameraPosition(), (float)fbw, (float)fbh);
	}

	// Update mouse drag
	if (scene.GetActiveDragAxis() != 0)
	{
		scene.HandleMouseMove(pixelMouseX, pixelMouseY,
			projection, view, (float)fbw, (float)fbh);
	}

	lastLMBState = currentLMB;
}
