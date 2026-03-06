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

	// Use full window coordinates — the viewport renders directly to screen
	double mouseX, mouseY;
	glfwGetCursorPos(window.getWindow(), &mouseX, &mouseY);
	float screenW = (float)window.getBufferWidth();
	float screenH = (float)window.getBufferHeight();

	if (currentLMB && !lastLMBState && !wantCapture)
	{
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS,
			(float)mouseX, (float)mouseY, projection, view,
			camera.getCameraPosition(), screenW, screenH);
	}
	else if (!currentLMB && lastLMBState)
	{
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE,
			0, 0, projection, view,
			camera.getCameraPosition(), 0, 0);
	}

	// Update mouse drag
	if (scene.GetActiveDragAxis() != 0)
	{
		scene.HandleMouseMove((float)mouseX, (float)mouseY,
			projection, view, screenW, screenH);
	}

	lastLMBState = currentLMB;
}
