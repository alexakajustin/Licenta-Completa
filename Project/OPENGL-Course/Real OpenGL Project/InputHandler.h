#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class Window;
class Camera;
class SceneManager;
class EditorUI;

class InputHandler
{
public:
	InputHandler();
	~InputHandler();

	// Camera controls only (call BEFORE rendering)
	void UpdateCamera(Window& window, Camera& camera, GLfloat deltaTime);

	// Editor picking & gizmo (call AFTER UI rendering so "Scene" window exists)
	void UpdateEditor(Window& window, Camera& camera, SceneManager& scene,
					  const glm::mat4& projection, const EditorUI& editorUI);

	// Box selection accessors (for overlay drawing)
	bool IsBoxSelecting() const { return boxSelecting; }
	glm::vec2 GetBoxSelectStart() const { return boxSelectStart; }
	glm::vec2 GetBoxSelectEnd() const { return boxSelectEnd; }

private:
	bool lastLMBState;

	// Box selection state
	bool boxSelecting = false;
	bool boxDragStarted = false;        // LMB pressed, waiting for threshold
	glm::vec2 boxSelectStart = glm::vec2(0.0f);
	glm::vec2 boxSelectEnd = glm::vec2(0.0f);
	glm::vec2 boxSelectScreenStart = glm::vec2(0.0f); // Absolute screen coords for threshold check
	static constexpr float BOX_SELECT_THRESHOLD = 5.0f;
};
