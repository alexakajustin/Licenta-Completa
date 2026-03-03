#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class Window;
class Camera;
class SceneManager;

class InputHandler
{
public:
	InputHandler();
	~InputHandler();

	void Update(Window& window, Camera& camera, SceneManager& scene,
				const glm::mat4& projection, GLfloat deltaTime);

private:
	bool lastLMBState;
};
