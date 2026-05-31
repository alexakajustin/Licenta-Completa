#include "Scene/Player.h"
#include "Core/Camera.h"
#include "Core/Window.h"
#include "Scene/SceneManager.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// =====================================================================
// Construction
// =====================================================================

Player::Player(const std::string& name)
	: GameObject(name)
{
}

Player::~Player()
{
}

// =====================================================================
// Core Update
// =====================================================================

void Player::Update(float deltaTime, Window& window, SceneManager& scene, Camera& camera)
{
	// 1. Mouse look
	ProcessMouseLook(window.getXChange(), window.getYChange());

	// 2. Horizontal movement (WASD)
	glm::vec3 moveDelta = ProcessKeyboard(window.getKeys(), deltaTime);

	// 3. Apply horizontal movement to the player's transform
	glm::vec3 pos = GetTransform().GetPosition();
	pos.x += moveDelta.x;
	pos.z += moveDelta.z;

	// 4. Gravity & jumping
	float groundY = QueryGroundHeight(pos, scene);

	if (grounded)
	{
		verticalVelocity = 0.0f;

		// Jump
		if (window.getKeys()[GLFW_KEY_SPACE])
		{
			verticalVelocity = jumpForce;
			grounded = false;
		}
	}
	else
	{
		verticalVelocity -= gravity * deltaTime;
	}

	pos.y += verticalVelocity * deltaTime;

	// 5. Ground collision
	if (pos.y <= groundY)
	{
		pos.y = groundY;
		verticalVelocity = 0.0f;
		grounded = true;
	}

	// 6. Write back position to the transform
	GetTransform().SetPosition(pos);

	// 7. Sync camera: position = player feet + eye height, orientation = yaw/pitch
	glm::vec3 eyePos = pos + glm::vec3(0.0f, eyeHeight, 0.0f);
	camera.setCameraPosition(eyePos);
	camera.setYaw(yaw);
	camera.setPitch(pitch);
	camera.update();
}

// =====================================================================
// State Reset
// =====================================================================

void Player::ResetPlayState(float initialYaw, float initialPitch)
{
	yaw = initialYaw;
	pitch = initialPitch;
	verticalVelocity = 0.0f;
	grounded = true;
}

// =====================================================================
// Input Processing
// =====================================================================

glm::vec3 Player::ProcessKeyboard(bool* keys, float deltaTime) const
{
	float speed = moveSpeed;

	// Sprint with Shift
	if (keys[GLFW_KEY_LEFT_SHIFT])
		speed *= 2.5f;

	glm::vec3 flatFront = GetFlatFront();
	glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

	glm::vec3 delta(0.0f);

	if (keys[GLFW_KEY_W]) delta += flatFront;
	if (keys[GLFW_KEY_S]) delta -= flatFront;
	if (keys[GLFW_KEY_D]) delta += flatRight;
	if (keys[GLFW_KEY_A]) delta -= flatRight;

	if (glm::length(delta) > 0.001f)
		delta = glm::normalize(delta) * speed * deltaTime;

	return delta;
}

void Player::ProcessMouseLook(float xChange, float yChange)
{
	yaw   += xChange * turnSpeed;
	pitch += yChange * turnSpeed;

	// Clamp pitch to prevent flipping
	if (pitch > 89.0f)  pitch = 89.0f;
	if (pitch < -89.0f) pitch = -89.0f;
}

// =====================================================================
// Terrain Query
// =====================================================================

float Player::QueryGroundHeight(const glm::vec3& position, SceneManager& scene) const
{
	// Simple flat ground at Y=0 for now.
	// TODO: Enhance with heightmap or raycast-based terrain queries.
	(void)position;
	(void)scene;
	return 0.0f;
}

// =====================================================================
// Direction Helpers
// =====================================================================

glm::vec3 Player::GetFlatFront() const
{
	// Movement direction: yaw only, no pitch (stays on the horizontal plane)
	glm::vec3 dir;
	dir.x = cos(glm::radians(yaw));
	dir.y = 0.0f;
	dir.z = sin(glm::radians(yaw));
	return glm::normalize(dir);
}

glm::vec3 Player::GetLookDirection() const
{
	// Full look direction including pitch (for camera)
	glm::vec3 dir;
	dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	dir.y = sin(glm::radians(pitch));
	dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	return glm::normalize(dir);
}
