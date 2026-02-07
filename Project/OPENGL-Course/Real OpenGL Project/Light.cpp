#include "Light.h"

Light::~Light()
{
}

Light::Light()
{
	colour = glm::vec3(1.0f, 1.0f, 1.0f);
	ambientIntensity = 1.0f;
	diffuseIntensity = 0.0f;
}

Light::Light(GLfloat shadowWidth, GLfloat shadowHeight, GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity, GLfloat diffuseIntensity)
{
	shadowMap = new ShadowMap();
	shadowMap->Init(shadowWidth, shadowHeight);

	colour = glm::vec3(red, green, blue);
	this->ambientIntensity = ambientIntensity;
	this->diffuseIntensity = diffuseIntensity;
}
