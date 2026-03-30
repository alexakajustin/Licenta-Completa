#include "DirectionalLight.h"

DirectionalLight::DirectionalLight() : Light()
{
	direction = glm::vec3(0.0f, -1.0f, 0.0f);
	lightProj = glm::ortho(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 200.0f);
}

DirectionalLight::DirectionalLight(GLfloat shadowWidth, GLfloat shadowHeight,
	GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity,
	GLfloat diffuseIntensity, GLfloat xDirection, GLfloat yDirection, GLfloat zDirection) : Light(shadowWidth, shadowHeight, red, green, blue, ambientIntensity, diffuseIntensity)
{
	direction = glm::vec3(xDirection, yDirection, zDirection);
	lightProj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f);
}

void DirectionalLight::UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation, GLuint diffuseIntensityLocation, GLuint directionLocation)
{
	glUniform3f(ambientColourLocation, colour.x, colour.y, colour.z);
	glUniform1f(ambientIntensityLocation, ambientIntensity);
	
	glUniform3f(directionLocation, direction.x, direction.y, direction.z);
	glUniform1f(diffuseIntensityLocation, diffuseIntensity);
}

void DirectionalLight::SetShadowFrustum(float size, float near, float far)
{
	lightProj = glm::ortho(-size, size, -size, size, near, far);
}

glm::mat4 DirectionalLight::CalculateLightTransform(glm::vec3 target)
{
	// Center the shadow frustum on the target (e.g. camera) to provide high-quality shadows around the viewer.
	// We move the shadow "eye" back from the target along the light ray.
	return lightProj * glm::lookAt(target + glm::normalize(-direction) * 100.0f, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

DirectionalLight::~DirectionalLight()
{
}


