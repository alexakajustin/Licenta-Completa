#pragma once

#include <GL/glew.h>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include <GLFW/glfw3.h>

/**
 * @class Camera
 * @brief Class representing a 3D camera with translation, Euler angles rotation, and zoom/scroll controls.
 */
class Camera
{
public:
	/**
	 * @brief Default constructor creating a camera at origin.
	 */
	Camera();

	/**
	 * @brief Custom constructor for detailed camera initialization.
	 * @param startPosition Initial 3D position vector.
	 * @param startUp World up vector (usually [0,1,0]).
	 * @param startYaw Initial yaw angle in degrees.
	 * @param startPitch Initial pitch angle in degrees.
	 * @param startMoveSpeed Initial translation speed.
	 * @param startTurnSpeed Initial rotation sensitivity.
	 */
	Camera(glm::vec3 startPosition, glm::vec3 startUp, GLfloat startYaw, GLfloat startPitch, GLfloat startMoveSpeed, GLfloat startTurnSpeed);

	/**
	 * @brief Destructor.
	 */
	~Camera();

	/**
	 * @brief Processes keyboard inputs to move the camera.
	 * @param keys Array of boolean flags indicating key press states.
	 * @param deltaTime Elapsed time between frames.
	 */
	void keyControl(bool* keys, GLfloat deltaTime);

	/**
	 * @brief Processes mouse movement changes to rotate the camera.
	 * @param xChange Delta movement on X axis.
	 * @param yChange Delta movement on Y axis.
	 */
	void mouseControl(GLfloat xChange, GLfloat yChange);

	/**
	 * @brief Processes mouse scroll delta for zoom adjustments or speed changes.
	 * @param yOffset Delta scroll offset.
	 */
	void scrollControl(GLfloat yOffset);

	/**
	 * @brief Retrieves the movement speed.
	 * @return Camera movement speed.
	 */
	GLfloat getMoveSpeed() const { return startMoveSpeed; }

	/**
	 * @brief Sets the movement speed.
	 * @param speed New movement speed value.
	 */
	void setMoveSpeed(GLfloat speed) { startMoveSpeed = speed; }

	/**
	 * @brief Gets the current 3D position of the camera.
	 * @return Position vector.
	 */
	glm::vec3 getCameraPosition();

	/**
	 * @brief Gets the front vector pointing towards the camera view direction.
	 * @return Direction vector.
	 */
	glm::vec3 getCameraDirection();
	
	/**
	 * @brief Gets the Yaw rotation angle.
	 * @return Yaw angle in degrees.
	 */
	GLfloat getYaw() const { return yaw; }

	/**
	 * @brief Gets the Pitch rotation angle.
	 * @return Pitch angle in degrees.
	 */
	GLfloat getPitch() const { return pitch; }
	
	/**
	 * @brief Manually sets the camera position.
	 * @param pos New position vector.
	 */
	void setCameraPosition(glm::vec3 pos) { position = pos; }

	/**
	 * @brief Manually sets the Yaw angle.
	 * @param y New Yaw angle in degrees.
	 */
	void setYaw(GLfloat y) { yaw = y; }

	/**
	 * @brief Manually sets the Pitch angle.
	 * @param p New Pitch angle in degrees.
	 */
	void setPitch(GLfloat p) { pitch = p; }

	/**
	 * @brief Computes the look-at view matrix for the shader pipeline.
	 * @return View transformation matrix.
	 */
	glm::mat4 calculateViewMatrix();

	/**
	 * @brief repositions the camera and points it directly at a target coordinate.
	 * @param targetPos Target position vector to look at.
	 * @param distance Distance offset from the target.
	 */
	void SetPositionAndLookAt(glm::vec3 targetPos, float distance = 5.0f);
	
	/**
	 * @brief Recalculates coordinate basis vectors (front, right, up) from Euler angles.
	 */
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
