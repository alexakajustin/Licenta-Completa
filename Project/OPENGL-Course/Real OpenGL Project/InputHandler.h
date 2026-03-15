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

private:
	bool lastLMBState;
};
