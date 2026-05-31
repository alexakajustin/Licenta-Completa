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
	glm::vec3 moveDelta(0.0f);

	// Only process input if cursor is locked (gameplay active)
	if (!window.isCursorEnabled())
	{
		// 1. Mouse look
		ProcessMouseLook(window.getXChange(), window.getYChange());

		// 2. Horizontal movement (WASD)
		moveDelta = ProcessKeyboard(window.getKeys(), deltaTime);

		// Jump
		if (grounded && window.getKeys()[GLFW_KEY_SPACE])
		{
			verticalVelocity = jumpForce;
			grounded = false;
		}
	}

	// 3. Apply horizontal movement to the player's transform
	glm::vec3 pos = gameObject->GetTransform().GetPosition();
	
	// Add proposed horizontal delta
	pos.x += moveDelta.x;
	pos.z += moveDelta.z;

	// Resolve Horizontal Collisions before applying gravity!
	ResolveHorizontalCollisions(pos, scene);

	// 4. Gravity & jumping
	float groundY = QueryGroundHeight(pos, scene);

	if (grounded && verticalVelocity <= 0.0f) // Only stick to ground if we are falling or resting
	{
		verticalVelocity = 0.0f;
	}
	else
	{
		verticalVelocity -= gravity * deltaTime;
	}

	pos.y += verticalVelocity * deltaTime;

	// 5. Ground collision
	if (pos.y <= groundY && verticalVelocity <= 0.0f)
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
// Horizontal Collision
// =====================================================================

// Helper to find closest point on triangle to a point
static glm::vec3 ClosestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
	glm::vec3 ab = b - a;
	glm::vec3 ac = c - a;
	glm::vec3 ap = p - a;

	float d1 = glm::dot(ab, ap);
	float d2 = glm::dot(ac, ap);
	if (d1 <= 0.0f && d2 <= 0.0f) return a;

	glm::vec3 bp = p - b;
	float d3 = glm::dot(ab, bp);
	float d4 = glm::dot(ac, bp);
	if (d3 >= 0.0f && d4 <= d3) return b;

	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		float v = d1 / (d1 - d3);
		return a + v * ab;
	}

	glm::vec3 cp = p - c;
	float d5 = glm::dot(ab, cp);
	float d6 = glm::dot(ac, cp);
	if (d6 >= 0.0f && d5 <= d6) return c;

	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		float w = d2 / (d2 - d6);
		return a + w * ac;
	}

	float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return b + w * (c - b);
	}

	float denom = 1.0f / (va + vb + vc);
	float v = vb * denom;
	float w = vc * denom;
	return a + ab * v + ac * w;
}

void Player::ResolveHorizontalCollisions(glm::vec3& position, SceneManager& scene) const
{
	glm::vec3 playerCenter = position + glm::vec3(0.0f, eyeHeight * 0.5f, 0.0f);

	for (GameObject* obj : scene.GetObjects())
	{
		if (!obj || !obj->GetVisible() || obj == gameObject) continue;

		// 1. Box Collider (Simplified as AABB for now, assuming mostly axis-aligned)
		BoxCollider* box = obj->GetComponent<BoxCollider>();
		if (box && !box->isTrigger)
		{
			glm::mat4 worldMat = obj->GetWorldMatrix();
			glm::vec3 center = glm::vec3(worldMat * glm::vec4(box->offset, 1.0f));
			glm::vec3 scale = obj->GetTransform().GetScale();
			glm::vec3 halfExtents = (box->size * scale) * 0.5f;

			glm::vec3 minP = center - halfExtents;
			glm::vec3 maxP = center + halfExtents;

			// Check vertical overlap
			if (position.y + eyeHeight > minP.y && position.y < maxP.y)
			{
				glm::vec3 closest(
					glm::clamp(position.x, minP.x, maxP.x),
					playerCenter.y,
					glm::clamp(position.z, minP.z, maxP.z)
				);

				glm::vec3 diff = glm::vec3(position.x, playerCenter.y, position.z) - closest;
				float distSq = diff.x * diff.x + diff.z * diff.z;

				if (distSq < radius * radius && distSq > 0.00001f)
				{
					float dist = std::sqrt(distSq);
					glm::vec3 pushDir = diff / dist;
					float pushDist = radius - dist;
					position.x += pushDir.x * pushDist;
					position.z += pushDir.z * pushDist;
				}
				else if (distSq <= 0.00001f) // inside
				{
					float dx0 = position.x - minP.x;
					float dx1 = maxP.x - position.x;
					float dz0 = position.z - minP.z;
					float dz1 = maxP.z - position.z;

					float minD = std::min({ dx0, dx1, dz0, dz1 });
					if (minD == dx0) position.x = minP.x - radius;
					else if (minD == dx1) position.x = maxP.x + radius;
					else if (minD == dz0) position.z = minP.z - radius;
					else position.z = maxP.z + radius;
				}
			}
		}

		// 2. Mesh Collider
		MeshCollider* mesh = obj->GetComponent<MeshCollider>();
		if (mesh)
		{
			const MeshData& md = obj->GetCPUMeshData();
			int triCount = md.GetTriangleCount();
			if (triCount == 0) continue;

			glm::mat4 worldMat = obj->GetWorldMatrix();
			glm::vec3 minBounds, maxBounds;
			md.GetBounds(minBounds, maxBounds);

			glm::vec3 wMin = glm::vec3(worldMat * glm::vec4(minBounds, 1.0f));
			glm::vec3 wMax = glm::vec3(worldMat * glm::vec4(maxBounds, 1.0f));
			if (wMin.x > wMax.x) std::swap(wMin.x, wMax.x);
			if (wMin.y > wMax.y) std::swap(wMin.y, wMax.y);
			if (wMin.z > wMax.z) std::swap(wMin.z, wMax.z);

			if (position.x + radius < wMin.x || position.x - radius > wMax.x ||
				position.y + eyeHeight < wMin.y || position.y > wMax.y ||
				position.z + radius < wMin.z || position.z - radius > wMax.z)
				continue;

			for (int i = 0; i < triCount; ++i)
			{
				glm::vec3 v0, v1, v2;
				md.GetTriangle(i, v0, v1, v2);

				v0 = glm::vec3(worldMat * glm::vec4(v0, 1.0f));
				v1 = glm::vec3(worldMat * glm::vec4(v1, 1.0f));
				v2 = glm::vec3(worldMat * glm::vec4(v2, 1.0f));

				glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
				if (std::abs(normal.y) > 0.7f) continue; // Skip steep floor/ceilings

				float tMinX = std::min({v0.x, v1.x, v2.x});
				float tMaxX = std::max({v0.x, v1.x, v2.x});
				float tMinY = std::min({v0.y, v1.y, v2.y});
				float tMaxY = std::max({v0.y, v1.y, v2.y});
				float tMinZ = std::min({v0.z, v1.z, v2.z});
				float tMaxZ = std::max({v0.z, v1.z, v2.z});

				if (position.x + radius < tMinX || position.x - radius > tMaxX ||
					position.y + eyeHeight < tMinY || position.y > tMaxY ||
					position.z + radius < tMinZ || position.z - radius > tMaxZ)
					continue;
				
				glm::vec3 samples[3] = {
					glm::vec3(position.x, position.y + 0.1f, position.z),
					glm::vec3(position.x, position.y + eyeHeight * 0.5f, position.z),
					glm::vec3(position.x, position.y + eyeHeight - 0.1f, position.z)
				};

				for (int s = 0; s < 3; ++s) {
					glm::vec3 closest = ClosestPointOnTriangle(samples[s], v0, v1, v2);
					glm::vec3 diff = samples[s] - closest;
					diff.y = 0.0f; // horizontal push
					float distSq = diff.x * diff.x + diff.z * diff.z;

					if (distSq < radius * radius && distSq > 0.00001f)
					{
						float dist = std::sqrt(distSq);
						glm::vec3 pushDir = diff / dist;
						float pushDist = radius - dist;
						position.x += pushDir.x * pushDist;
						position.z += pushDir.z * pushDist;
						
						for (int j = 0; j < 3; ++j) {
							samples[j].x += pushDir.x * pushDist;
							samples[j].z += pushDir.z * pushDist;
						}
					}
				}
			}
		}
	}
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
