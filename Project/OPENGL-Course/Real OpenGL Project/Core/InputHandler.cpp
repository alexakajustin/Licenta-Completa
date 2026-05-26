#include "Core/InputHandler.h"
#include "Core/Window.h"
#include "Core/Camera.h"
#include "Scene/SceneManager.h"
#include "Editor/EditorUI.h"

#include "imgui.h"
#include <algorithm>

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

		float scroll = ImGui::GetIO().MouseWheel;
		if (scroll != 0.0f)
		{
			camera.scrollControl(scroll);
		}
	}
}

void InputHandler::UpdateEditor(Window& window, Camera& camera, SceneManager& scene,
								const glm::mat4& projection, const EditorUI& editorUI, GLuint viewportFBO)
{
	if (!window.isCursorEnabled())
	{
		lastLMBState = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];
		boxSelecting = false;
		boxDragStarted = false;
		scene.SetBoxSelecting(false);
		return;
	}

	// Viewport-relative mouse logic
	glm::vec2 vPos = editorUI.GetViewportPos();
	glm::vec2 vSize = editorUI.GetViewportSize();
	bool vHovered = editorUI.IsViewportHovered();

	// Editor mode — handle picking and gizmo dragging
	bool currentLMB = window.getMouseButtons()[GLFW_MOUSE_BUTTON_LEFT];

	// We only want to interact if we are hovering the viewport OR already dragging a gizmo/box
	bool isDraggingGizmo = (scene.GetActiveDragAxis() != 0);
	if (!vHovered && !isDraggingGizmo && !boxSelecting && !boxDragStarted) {
		lastLMBState = currentLMB;
		return;
	}

	glm::mat4 view = camera.calculateViewMatrix();

	// Get raw mouse position from GLFW (in screen/window points — same space as ImGui)
	double mouseX, mouseY;
	glfwGetCursorPos(window.getWindow(), &mouseX, &mouseY);

	// Use screen-point coordinates directly — ImGui viewport position (vPos)
	// is in screen points, not framebuffer pixels. Scaling by the framebuffer/window
	// ratio would create a mismatch on High DPI displays (125%, 150%, 200% scaling),
	// causing depth buffer reads from the wrong pixel and broken occlusion culling.
	float screenX = (float)mouseX;
	float screenY = (float)mouseY;

	// Offset by viewport position (both in screen points now)
	float relativeX = screenX - vPos.x;
	float relativeY = screenY - vPos.y;

	if (currentLMB && !lastLMBState)
	{
		// LMB just pressed — only check for gizmo handles, defer full pick to release
		// This prevents selecting a single object before box select can start
		scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS,
			relativeX, relativeY, projection, view,
			camera.getCameraPosition(), vSize.x, vSize.y);

		// If we grabbed a gizmo, don't start box select tracking
		if (scene.GetActiveDragAxis() == 0 && vHovered) {
			boxDragStarted = true;
			boxSelectStart = glm::vec2(relativeX, relativeY);
			boxSelectEnd = boxSelectStart;
			boxSelectScreenStart = glm::vec2(screenX, screenY);
		}
	}
	else if (currentLMB && lastLMBState)
	{
		// LMB held — check for box select threshold or continue dragging
		if (boxDragStarted && !boxSelecting) {
			float dx = screenX - boxSelectScreenStart.x;
			float dy = screenY - boxSelectScreenStart.y;
			if (dx * dx + dy * dy > BOX_SELECT_THRESHOLD * BOX_SELECT_THRESHOLD) {
				boxSelecting = true;
				scene.SetBoxSelecting(true);
			}
		}

		if (boxSelecting) {
			boxSelectEnd = glm::vec2(relativeX, relativeY);
		}
	}
	else if (!currentLMB && lastLMBState)
	{
		// LMB released
		if (boxSelecting) {
			// Finalize box selection
			boxSelectEnd = glm::vec2(relativeX, relativeY);

			// Build min/max rectangle
			glm::vec2 rectMin(
				std::min(boxSelectStart.x, boxSelectEnd.x),
				std::min(boxSelectStart.y, boxSelectEnd.y)
			);
			glm::vec2 rectMax(
				std::max(boxSelectStart.x, boxSelectEnd.x),
				std::max(boxSelectStart.y, boxSelectEnd.y)
			);

			// Check if Shift is held for additive selection
			bool shiftHeld = glfwGetKey(window.getWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
							 glfwGetKey(window.getWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

			scene.BoxSelect(rectMin, rectMax, projection, view, vSize.x, vSize.y, shiftHeld, viewportFBO);

			boxSelecting = false;
			boxDragStarted = false;
			scene.SetBoxSelecting(false);
		}
		else {
			// Normal single-click release (no drag happened) or gizmo end
			scene.HandleMousePress(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE,
				0, 0, projection, view,
				camera.getCameraPosition(), vSize.x, vSize.y);
			boxDragStarted = false;
		}
	}

	// Update mouse drag for gizmo
	if (scene.GetActiveDragAxis() != 0)
	{
		scene.HandleMouseMove(relativeX, relativeY,
			projection, view, vSize.x, vSize.y);
	}

	lastLMBState = currentLMB;
}

