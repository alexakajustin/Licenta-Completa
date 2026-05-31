#include "Scene/Player.h"
#include "Core/Camera.h"
#include "Core/Window.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Scene/RigidBody.h"
#include "Scene/CapsuleCollider.h"
#include "Simulation/PhysicsSystem.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <imgui.h>

// =====================================================================
// Lifecycle
// =====================================================================

Player::Player(GameObject* owner)
	: Component(owner)
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
	if (!mPhysicsInitialized) {
		CapsuleCollider* cap = gameObject->GetComponent<CapsuleCollider>();
		if (!cap) {
			cap = gameObject->AddComponent<CapsuleCollider>();
			cap->radius = radius;
			cap->height = eyeHeight * 1.5f; 
			cap->offset = glm::vec3(0.0f, cap->height * 0.5f, 0.0f);
		}
		
		RigidBody* rb = gameObject->GetComponent<RigidBody>();
		if (!rb) {
			rb = gameObject->AddComponent<RigidBody>();
			rb->SetType(RigidBody::BodyType::Dynamic);
			rb->SetLockRotation(true);
			rb->SetMass(80.0f);
		}
		mPhysicsInitialized = true;
	}

	RigidBody* rb = gameObject->GetComponent<RigidBody>();
	if (!rb) return;

	// Only process input if cursor is locked (gameplay active)
	if (!window.isCursorEnabled())
	{
		// 1. Mouse look
		ProcessMouseLook(window.getXChange(), window.getYChange());

		// 2. Horizontal movement (WASD) - returns target velocity
		glm::vec3 targetVel = ProcessKeyboard(window.getKeys(), deltaTime);

		auto& bodyInterface = PhysicsSystem::GetInstance().GetBodyInterface();
		JPH::Vec3 currentVel = bodyInterface.GetLinearVelocity(rb->GetBodyID());

		// Determine grounded by raycast or just simple velocity check
		// For simplicity, if vertical velocity is very low, assume grounded. 
		bool isGrounded = std::abs(currentVel.GetY()) < 0.1f;
		
		float newVelY = currentVel.GetY();

		// Jump
		if (isGrounded && window.getKeys()[GLFW_KEY_SPACE])
		{
			newVelY = jumpForce;
		}

		rb->SetLinearVelocity(glm::vec3(targetVel.x, newVelY, targetVel.z));
	}

	// The position is updated by the RigidBody component automatically
	glm::vec3 pos = gameObject->GetTransform().GetPosition();

	// Sync camera: position = player feet + eye height, orientation = yaw/pitch
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
		delta = glm::normalize(delta) * speed;

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

void Player::DrawInspector()
{
	if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 0.0f, 100.0f);
		ImGui::DragFloat("Turn Speed", &turnSpeed, 0.1f, 0.0f, 1000.0f);
		ImGui::DragFloat("Eye Height", &eyeHeight, 0.05f, 0.1f, 10.0f);
		ImGui::DragFloat("Radius", &radius, 0.05f, 0.1f, 5.0f);
		ImGui::DragFloat("Jump Force", &jumpForce, 0.1f, 0.0f, 50.0f);
		ImGui::DragFloat("Gravity", &gravity, 0.1f, 0.0f, 100.0f);

		ImGui::Text("State:");
		ImGui::Text("Grounded: %s", grounded ? "Yes" : "No");
		ImGui::Text("Vertical Vel: %.2f", verticalVelocity);
	}
}
