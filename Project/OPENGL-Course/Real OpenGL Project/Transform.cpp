#include "Transform.h"

Transform::Transform()
	: position(glm::vec3(0.0f)), rotation(glm::vec3(0.0f)), scale(glm::vec3(1.0f))
{
}

Transform::Transform(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale)
	: position(position), rotation(rotation), scale(scale)
{
}

Transform::~Transform()
{
}

glm::mat4 Transform::GetModelMatrix() const
{
	glm::mat4 model(1.0f);

	// Translation
	model = glm::translate(model, position);

	// Rotation (applied in order: Y, X, Z - Yaw, Pitch, Roll)
	model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

	// Scale
	model = glm::scale(model, scale);

	return model;
}

void Transform::SetFromMatrix(const glm::mat4& matrix)
{
	// Translation
	position = glm::vec3(matrix[3]);

	// Scale
	scale.x = glm::length(glm::vec3(matrix[0]));
	scale.y = glm::length(glm::vec3(matrix[1]));
	scale.z = glm::length(glm::vec3(matrix[2]));

	// Rotation (Orthonormalize the 3x3 part to get rotation)
	if (scale.x > 0.0001f && scale.y > 0.0001f && scale.z > 0.0001f) {
		glm::mat3 rotMat;
		rotMat[0] = glm::vec3(matrix[0]) / scale.x;
		rotMat[1] = glm::vec3(matrix[1]) / scale.y;
		rotMat[2] = glm::vec3(matrix[2]) / scale.z;

		// Convert rotation matrix to Euler angles
		// Consistent with Y-X-Z order in GetModelMatrix: 
		// model = Translate * RotateY * RotateX * RotateZ * Scale
		float radX = asin(glm::clamp(-rotMat[2][1], -1.0f, 1.0f));
		rotation.x = glm::degrees(radX);
		
		if (cos(radX) > 0.001f) {
			rotation.y = glm::degrees(atan2(rotMat[2][0], rotMat[2][2]));
			rotation.z = glm::degrees(atan2(rotMat[0][1], rotMat[1][1]));
		} else {
			// Gimbal lock case
			rotation.y = glm::degrees(atan2(-rotMat[0][2], rotMat[0][0]));
			rotation.z = 0.0f;
		}
	}
}
