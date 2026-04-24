#pragma once
#include "Light.h"
class DirectionalLight : public Light
{
public:
	DirectionalLight();

	DirectionalLight(GLfloat shadowWidth, GLfloat shadowHeight,
		GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity, GLfloat diffuseIntensity,
		GLfloat xDirection, GLfloat yDirection, GLfloat zDirection);

	void UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation,
		GLuint diffuseIntensityLocation, GLuint directionLocation);

	void SetShadowFrustum(float size, float near, float far);

	glm::mat4 CalculateLightTransform(glm::vec3 target);

	// Getter for editing
	glm::vec3* GetDirectionPtr() { return &direction; }
	float GetShadowFrustumSize() const { return shadowFrustumSize; }

	~DirectionalLight();
private:
	glm::vec3 direction;
	float shadowFrustumSize = 100.0f;
};


