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

	~DirectionalLight();
private:
	glm::vec3 direction;
};


