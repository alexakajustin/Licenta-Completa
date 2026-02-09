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

	// Rotation (applied in order: X, Y, Z)
	model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

	// Scale
	model = glm::scale(model, scale);

	return model;
}
