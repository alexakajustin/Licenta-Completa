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
		bool currentLMB = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];
		double mouseX, mouseY;
		glfwGetCursorPos(window.getWindow(), &mouseX, &mouseY);

		bool wantCapture = ImGui::GetIO().WantCaptureMouse;
		glm::mat4 view = camera.calculateViewMatrix();

		if (currentLMB && !lastLMBState && !wantCapture)
		{
			// With the new UI overhaul, we need the "Scene" window's bounds
			// We use ImGui::FindWindowByName (from imgui_internal.h) OR better, we can just check if the window is hovered
			// But for layout, we need to know where it is.
			
			// Actually, let's use a simpler approach: check if Scene window is focused/hovered
			if (ImGui::Begin("Scene")) {
				if (ImGui::IsWindowHovered()) {
					ImVec2 winPos = ImGui::GetWindowPos();
					ImVec2 winSize = ImGui::GetWindowSize();
					ImVec2 mousePos = ImGui::GetMousePos();

					float localX = mousePos.x - winPos.x;
					float localY = mousePos.y - winPos.y;

					scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, localX, localY, projection, view, camera.getCameraPosition(), winSize.x, winSize.y);
				}
				ImGui::End();
			}
		}
		else if (!currentLMB && lastLMBState)
		{
			scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0, 0, projection, view, camera.getCameraPosition(), 0, 0);
		}
		
		// Update mouse drag
		if (scene.GetActiveDragAxis() != 0)
		{
			if (ImGui::Begin("Scene")) {
				ImVec2 winPos = ImGui::GetWindowPos();
				ImVec2 winSize = ImGui::GetWindowSize();
				ImVec2 mousePos = ImGui::GetMousePos();
				float localX = mousePos.x - winPos.x;
				float localY = mousePos.y - winPos.y;
				scene.HandleMouseMove(localX, localY, projection, view, winSize.x, winSize.y);
				ImGui::End();
			}
		}

		lastLMBState = currentLMB;
	}
}
