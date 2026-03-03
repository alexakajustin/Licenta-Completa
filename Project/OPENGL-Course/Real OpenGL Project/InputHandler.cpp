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

void InputHandler::Update(Window& window, Camera& camera, SceneManager& scene,
						  const glm::mat4& projection, GLfloat deltaTime)
{
	if (!window.isCursorEnabled())
	{
		// FPS camera mode
		camera.keyControl(window.getKeys(), deltaTime);
		camera.mouseControl(window.getXChange(), window.getYChange());
	}
	else
	{
		// Editor mode — handle picking and gizmo dragging
		bool currentLMB = glfwGetMouseButton(window.getWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		double mouseX, mouseY;
		glfwGetCursorPos(window.getWindow(), &mouseX, &mouseY);

		if (!ImGui::GetIO().WantCaptureMouse)
		{
			glm::mat4 view = camera.calculateViewMatrix();

			if (currentLMB && !lastLMBState)
			{
				if (!ImGui::GetIO().WantCaptureMouse) {
					scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS,
						(float)mouseX, (float)mouseY, projection, view, camera.getCameraPosition());
				}
			}
			else if (!currentLMB && lastLMBState)
			{
				// Always handle release to clear gizmo state
				scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE,
					(float)mouseX, (float)mouseY, projection, view, camera.getCameraPosition());
			}
			else if (currentLMB)
			{
				if (!ImGui::GetIO().WantCaptureMouse) {
					scene.HandleMouseMove((float)mouseX, (float)mouseY, projection, view);
				}
			}
		}
		lastLMBState = currentLMB;
	}
}
