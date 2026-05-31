#include "Scene/Player.h"
#include "Core/Camera.h"
#include "Core/Window.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"

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
	// 1. Mouse look
	ProcessMouseLook(window.getXChange(), window.getYChange());

	// 2. Horizontal movement (WASD)
	glm::vec3 moveDelta = ProcessKeyboard(window.getKeys(), deltaTime);

	// 3. Apply horizontal movement to the player's transform
	glm::vec3 pos = gameObject->GetTransform().GetPosition();
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
	gameObject->GetTransform().SetPosition(pos);

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

#include "Scene/BoxCollider.h"
#include "Scene/MeshCollider.h"
#include "Scene/CapsuleCollider.h"

float Player::QueryGroundHeight(const glm::vec3& position, SceneManager& scene) const
{
	float highestGround = -1e10f;
	bool foundGround = false;

	// Ray from slightly above the player's feet straight down
	glm::vec3 rayOrigin = position + glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec3 rayDir = glm::vec3(0.0f, -1.0f, 0.0f);

	for (GameObject* obj : scene.GetObjects())
	{
		if (!obj || !obj->GetVisible() || obj == gameObject) continue;

		BoxCollider* collider = obj->GetComponent<BoxCollider>();
		if (collider && !collider->isTrigger)
		{
			glm::mat4 invWorld = glm::inverse(obj->GetWorldMatrix());
			glm::vec3 localOrigin = invWorld * glm::vec4(rayOrigin, 1.0f);
			glm::vec3 localDir = invWorld * glm::vec4(rayDir, 0.0f);

			glm::vec3 boxMin = collider->offset - collider->size * 0.5f;
			glm::vec3 boxMax = collider->offset + collider->size * 0.5f;

			float tmin = -1e10f;
			float tmax = 1e10f;
			bool hit = true;

			for (int i = 0; i < 3; ++i)
			{
				if (std::abs(localDir[i]) < 1e-6f)
				{
					if (localOrigin[i] < boxMin[i] || localOrigin[i] > boxMax[i])
					{
						hit = false;
						break;
					}
				}
				else
				{
					float t1 = (boxMin[i] - localOrigin[i]) / localDir[i];
					float t2 = (boxMax[i] - localOrigin[i]) / localDir[i];
					if (t1 > t2) std::swap(t1, t2);
					if (t1 > tmin) tmin = t1;
					if (t2 < tmax) tmax = t2;
					if (tmin > tmax)
					{
						hit = false;
						break;
					}
				}
			}

			// tmax > 0 ensures box is not behind the ray
			// tmin >= -0.5f ensures ray starts outside or slightly inside the top boundary
			if (hit && tmax >= 0.0f && tmin >= -0.5f)
			{
				float t = std::max(0.0f, tmin);
				glm::vec3 hitPoint = rayOrigin + rayDir * t;
				
				if (hitPoint.y > highestGround)
				{
					highestGround = hitPoint.y;
					foundGround = true;
				}
			}
		}

		MeshCollider* meshCollider = obj->GetComponent<MeshCollider>();
		if (meshCollider)
		{
			glm::mat4 invWorld = glm::inverse(obj->GetWorldMatrix());
			glm::vec4 localPos = invWorld * glm::vec4(position, 1.0f);

			const MeshData& md = obj->GetCPUMeshData();
			if (md.GetVertexCount() > 0)
			{
				float localHeight = md.GetHeightAt(localPos.x, localPos.z, 0.0f);
				if (localHeight > -1e9f)
				{
					glm::vec4 worldPos = obj->GetWorldMatrix() * glm::vec4(localPos.x, localHeight, localPos.z, 1.0f);
					if (worldPos.y > highestGround && worldPos.y < position.y + 1.5f)
					{
						highestGround = worldPos.y;
						foundGround = true;
					}
				}
			}
		}
	}

	// Fallback to 0.0f so player doesn't fall forever if they walk off the map
	return foundGround ? highestGround : 0.0f;
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
		ImGui::DragFloat("Eye Height", &eyeHeight, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Jump Force", &jumpForce, 0.1f, 0.0f, 50.0f);
		ImGui::DragFloat("Gravity", &gravity, 0.1f, 0.0f, 100.0f);
	}
}
