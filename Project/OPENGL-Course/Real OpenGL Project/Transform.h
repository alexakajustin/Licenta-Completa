#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Transform
{
public:
	Transform();
	Transform(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);
	~Transform();

	// Getters
	glm::vec3 GetPosition() const { return position; }
	glm::vec3 GetRotation() const { return rotation; }
	glm::vec3 GetScale() const { return scale; }

	// Setters
	void SetPosition(const glm::vec3& pos) { position = pos; }
	void SetRotation(const glm::vec3& rot) { rotation = rot; }
	void SetScale(const glm::vec3& scl) { scale = scl; }

	// Direct access for ImGui editing
	glm::vec3* GetPositionPtr() { return &position; }
	glm::vec3* GetRotationPtr() { return &rotation; }
	glm::vec3* GetScalePtr() { return &scale; }

	// Compute the model matrix (Translation * Rotation * Scale)
	glm::mat4 GetModelMatrix() const;

private:
	glm::vec3 position;
	glm::vec3 rotation; // Euler angles in degrees (pitch, yaw, roll)
	glm::vec3 scale;
};
