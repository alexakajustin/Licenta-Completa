#include "InputHandler.h"
#include "Window.h"
#include "Camera.h"
#include "SceneManager.h"
#include "EditorUI.h"

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
								const glm::mat4& projection, const EditorUI& editorUI)
{
	if (!window.isCursorEnabled())
	{
		lastLMBState = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];
		return;
	}

	// Viewport-relative mouse logic
	glm::vec2 vPos = editorUI.GetViewportPos();
	glm::vec2 vSize = editorUI.GetViewportSize();
	bool vHovered = editorUI.IsViewportHovered();

	// Editor mode — handle picking and gizmo dragging
	bool currentLMB = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];

	// We only want to pick if we are hovering the viewport OR already dragging a gizmo
	bool isDraggingGizmo = (scene.GetActiveDragAxis() != 0);
	if (!vHovered && !isDraggingGizmo) {
		lastLMBState = currentLMB;
		return;
	}

	glm::mat4 view = camera.calculateViewMatrix();

	// Get raw mouse position from GLFW
	double mouseX, mouseY;
	glfwGetCursorPos(window.getWindow(), &mouseX, &mouseY);

	// Translate to screen space (taking care of any window/framebuffer scaling)
	int winW, winH;
	glfwGetWindowSize(window.getWindow(), &winW, &winH);
	int fbw, fbh;
	glfwGetFramebufferSize(window.getWindow(), &fbw, &fbh);

	float screenX = (float)mouseX * ((float)fbw / (float)winW);
	float screenY = (float)mouseY * ((float)fbh / (float)winH);

	// Offset by viewport position
	float relativeX = screenX - vPos.x;
	float relativeY = screenY - vPos.y;

	if (currentLMB && !lastLMBState)
	{
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS,
			relativeX, relativeY, projection, view,
			camera.getCameraPosition(), vSize.x, vSize.y);
	}
	else if (!currentLMB && lastLMBState)
	{
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE,
			0, 0, projection, view,
			camera.getCameraPosition(), vSize.x, vSize.y);
	}

	// Update mouse drag
	if (scene.GetActiveDragAxis() != 0)
	{
		scene.HandleMouseMove(relativeX, relativeY,
			projection, view, vSize.x, vSize.y);
	}

	lastLMBState = currentLMB;
}
