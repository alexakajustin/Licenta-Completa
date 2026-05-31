#pragma once

#include "Scene/Component.h"
#include <glm/glm.hpp>

class Camera;
class Window;
class SceneManager;

/**
 * @class Player
 * @brief First-person player controller that drives a Camera during Play Mode.
 *
 * Design:
 *   - Inherits from Component so it can be attached to any GameObject.
 *   - Only ONE Player component should exist in a scene at a time.
 *   - During Play Mode the Application calls Update() each frame.
 */
class Player : public Component
{
public:
	Player(GameObject* owner);
	~Player();

	// ===== Core Update (called every frame during Play Mode) =====

	/**
	 * @brief Processes input, applies movement & physics, and syncs the camera.
	 * @param deltaTime  Frame delta in seconds.
	 * @param window     Window reference for keyboard/mouse state.
	 * @param scene      SceneManager reference for terrain queries.
	 * @param camera     Camera to drive (position + orientation are overwritten).
	 */
	void Update(float deltaTime, Window& window, SceneManager& scene, Camera& camera);

	// ===== Configuration =====

	float GetMoveSpeed()  const { return moveSpeed; }
	float GetTurnSpeed()  const { return turnSpeed; }
	float GetEyeHeight()  const { return eyeHeight; }
	float GetJumpForce()  const { return jumpForce; }
	float GetGravity()    const { return gravity; }

	void SetMoveSpeed(float v)  { moveSpeed = v; }
	void SetTurnSpeed(float v)  { turnSpeed = v; }
	void SetEyeHeight(float v)  { eyeHeight = v; }
	void SetJumpForce(float v)  { jumpForce = v; }
	void SetGravity(float v)    { gravity = v; }

	// ===== State Queries =====

	bool IsGrounded() const { return grounded; }
	float GetYaw()    const { return yaw; }
	float GetPitch()  const { return pitch; }

	/// Reset runtime state (called when entering Play Mode)
	void ResetPlayState(float initialYaw = -90.0f, float initialPitch = 0.0f);

	std::string GetName() const override { return "Player"; }
	void DrawInspector() override;

private:
	// ===== Movement Parameters =====
	float moveSpeed  = 5.0f;
	float turnSpeed  = 0.15f;
	float eyeHeight  = 1.7f;    // Camera offset above the player's feet position
	float radius     = 0.3f;    // Player horizontal collision radius
	float jumpForce  = 6.0f;
	float gravity    = 15.0f;

	// ===== Runtime State (not serialized) =====
	float yaw   = -90.0f;
	float pitch = 0.0f;
	float verticalVelocity = 0.0f;
	bool  grounded = true;

	// ===== Internal Helpers =====

	/// Read WASD/Space keys and produce a world-space movement delta (horizontal only).
	glm::vec3 ProcessKeyboard(bool* keys, float deltaTime) const;

	/// Read mouse deltas and update yaw/pitch, clamping pitch to [-89, 89].
	void ProcessMouseLook(float xChange, float yChange);

	/// Resolve horizontal collisions against Box, Capsule, and Mesh colliders.
	void ResolveHorizontalCollisions(glm::vec3& position, SceneManager& scene) const;

	/// Query the scene for the ground height directly beneath the given XZ position.
	/// Returns the Y coordinate of the terrain surface, or 0.0 if no terrain is found.
	float QueryGroundHeight(const glm::vec3& position, SceneManager& scene) const;

	/// Compute the front direction vector from yaw/pitch (horizontal only, for movement).
	glm::vec3 GetFlatFront() const;

	/// Compute the full front direction vector from yaw/pitch (for camera).
	glm::vec3 GetLookDirection() const;
};
