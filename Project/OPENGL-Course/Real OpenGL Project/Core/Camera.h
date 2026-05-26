#pragma once

#include <GL/glew.h>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include <GLFW/glfw3.h>

class Camera
{
public:
	Camera();
	Camera(glm::vec3 startPosition, glm::vec3 startUp, GLfloat startYaw, GLfloat startPitch, GLfloat startMoveSpeed, GLfloat startTurnSpeed);
	~Camera();

	void keyControl(bool* keys, GLfloat deltaTime);
	void mouseControl(GLfloat xChange, GLfloat yChange);
	void scrollControl(GLfloat yOffset);

	GLfloat getMoveSpeed() const { return startMoveSpeed; }
	void setMoveSpeed(GLfloat speed) { startMoveSpeed = speed; }

	glm::vec3 getCameraPosition();
	glm::vec3 getCameraDirection();
	
	GLfloat getYaw() const { return yaw; }
	GLfloat getPitch() const { return pitch; }
	
	void setCameraPosition(glm::vec3 pos) { position = pos; }
	void setYaw(GLfloat y) { yaw = y; }
	void setPitch(GLfloat p) { pitch = p; }

	glm::mat4 calculateViewMatrix();
	void SetPositionAndLookAt(glm::vec3 targetPos, float distance = 5.0f);
	
	void update(); // Made public for serialization
private:
	glm::vec3 position;
	glm::vec3 front;
	glm::vec3 up;
	glm::vec3 right;
	glm::vec3 worldUp;

	// left and right
	GLfloat yaw;
	// up and down
	GLfloat pitch;

	GLfloat moveSpeed;
	GLfloat turnSpeed;
	GLfloat startMoveSpeed;

	// void update(); // Moved to public
};
