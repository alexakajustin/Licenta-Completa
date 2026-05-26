#include "Core/Camera.h"

Camera::Camera()
{

}

Camera::Camera(glm::vec3 startPosition, glm::vec3 startUp, GLfloat startYaw, GLfloat startPitch, GLfloat startMoveSpeed, GLfloat startTurnSpeed)
{
	position = startPosition;
	worldUp = startUp;
	yaw = startYaw;
	pitch = startPitch;
	front = glm::vec3(0.0f, 0.0f, -1.0f);

	moveSpeed = startMoveSpeed;
	turnSpeed = startTurnSpeed;
	this->startMoveSpeed = startMoveSpeed;

	update();
}

Camera::~Camera()
{
}

void Camera::keyControl(bool* keys, GLfloat deltaTime)
{
	GLfloat velocity;
	if (keys[GLFW_KEY_LEFT_SHIFT])
	{
		velocity = (startMoveSpeed * 3) * deltaTime;
	}
	else 
	{
		velocity = startMoveSpeed * deltaTime;
	}

	if (keys[GLFW_KEY_W])
	{
		position += front * velocity;
	}
	if (keys[GLFW_KEY_S])
	{
		position -= front * velocity;
	}
	if (keys[GLFW_KEY_A])
	{
		position -= right * velocity;
	}
	if (keys[GLFW_KEY_D])
	{
		position += right * velocity;
	}
	if (keys[GLFW_KEY_SPACE])
	{
		position += up * velocity;
	}
	if (keys[GLFW_KEY_LEFT_CONTROL])
	{
		position -= up * velocity;
	}
}

void Camera::scrollControl(GLfloat yOffset)
{
	// Change speed exponentially for better control (Unity style)
	if (yOffset > 0) {
		startMoveSpeed *= 1.1f;
	} else if (yOffset < 0) {
		startMoveSpeed /= 1.1f;
	}

	// Clamp speed to reasonable values
	if (startMoveSpeed < 0.1f) startMoveSpeed = 0.1f;
	if (startMoveSpeed > 100.0f) startMoveSpeed = 100.0f;
}

void Camera::mouseControl(GLfloat xChange, GLfloat yChange)
{
	xChange *= turnSpeed;
	yChange *= turnSpeed;

	yaw += xChange;
	pitch += yChange;

	if (pitch > 89.0f)
	{
		pitch = 89.0f;
	}

	if (pitch < -89.0f) {
		pitch = -89.0f;
	}

	update();
}

glm::vec3 Camera::getCameraPosition()
{
	return position;
}

glm::vec3 Camera::getCameraDirection()
{
	return glm::normalize(front);
}

glm::mat4 Camera::calculateViewMatrix()
{
	return glm::lookAt(position, position + front, up);
}

void Camera::update()
{
	// need to see why its the way it is
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	// squeeze into [-1, 1]
	front = glm::normalize(front);

	right = glm::normalize(glm::cross(front, worldUp));
	up = glm::normalize(glm::cross(right, front));
}

void Camera::SetPositionAndLookAt(glm::vec3 targetPos, float distance)
{
	// Use a fixed "Unity-like" top-down diagonal angle (Yaw: -45, Pitch: -30)
	yaw = -45.0f;
	pitch = -30.0f;

	// Update vectors based on these fixed angles
	update();

	// Position the camera 'distance' units away along the new 'front' vector
	position = targetPos - front * distance;

	update(); // Final refresh
}
