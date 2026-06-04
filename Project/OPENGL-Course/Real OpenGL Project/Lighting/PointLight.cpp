#include "Lighting/PointLight.h"

PointLight::PointLight() : Light()
{
	position = glm::vec3(0.0f, 0.0f, 0.0f);
	constant = 1.0f;
	linear = 0.0f;
	exponent = 0.0f;
	//L / (ax^2 + bx + c)
}

PointLight::PointLight(GLfloat shadowWidth, GLfloat shadowHeight,
	GLfloat near, GLfloat far,
	GLfloat red, GLfloat green, GLfloat blue,
	GLfloat ambientIntensity, GLfloat diffuseIntensity,
	GLfloat xPosition, GLfloat yPosition, GLfloat zPosition, 
	GLfloat constant, GLfloat linear, GLfloat exponent) : Light(shadowWidth, shadowHeight, red, green, blue, ambientIntensity, diffuseIntensity)
{
	this->position = glm::vec3(xPosition, yPosition, zPosition);
	this->constant = constant;
	this->linear = linear;
	this->exponent = exponent;

	farPlane = far;

	float aspect = (float)shadowWidth / (float)shadowHeight;

	lightProj = glm::perspective(glm::radians(90.0f), aspect, near, far);

	shadowMap = std::make_unique<OmniShadowMap>();
	shadowMap->Init(shadowWidth, shadowHeight);
}

void PointLight::UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation, GLuint diffuseIntensityLocation, GLuint positionLocation,
  GLuint constantLocation, GLuint linearLocation, GLuint exponentLocation)
{
	if (ambientColourLocation != (GLuint)-1) glUniform3f(ambientColourLocation, colour.x, colour.y, colour.z);
	if (ambientIntensityLocation != (GLuint)-1) glUniform1f(ambientIntensityLocation, ambientIntensity);
	if (diffuseIntensityLocation != (GLuint)-1) glUniform1f(diffuseIntensityLocation, diffuseIntensity);
	
	if (positionLocation != (GLuint)-1) glUniform3f(positionLocation, position.x, position.y, position.z);

	if (constantLocation != (GLuint)-1) glUniform1f(constantLocation, constant);
	if (linearLocation != (GLuint)-1) glUniform1f(linearLocation, linear);
	if (exponentLocation != (GLuint)-1) glUniform1f(exponentLocation, exponent);
}

GLfloat PointLight::GetFarPlane()
{
	return farPlane;
}

std::vector<glm::mat4> PointLight::CalculateLightTransform()
{
	std::vector<glm::mat4> lightMatrices;
	// +x
	lightMatrices.push_back(lightProj * glm::lookAt(position, position + glm::vec3(1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
	// -x
	lightMatrices.push_back(lightProj * glm::lookAt(position, position + glm::vec3(-1.0, 0.0, 0.0), glm::vec3(0.0, -1.0, 0.0)));
	// +y
	lightMatrices.push_back(lightProj * glm::lookAt(position, position + glm::vec3(0.0,1.0, 0.0), glm::vec3(0.0, 0.0, 1.0)));
	// -y
	lightMatrices.push_back(lightProj * glm::lookAt(position, position + glm::vec3(0.0, -1.0, 0.0), glm::vec3(0.0, 0.0, -1.0)));
	// +z
	lightMatrices.push_back(lightProj * glm::lookAt(position, position + glm::vec3(0.0, 0.0, 1.0), glm::vec3(0.0, -1.0, 0.0)));
	// -z
	lightMatrices.push_back(lightProj * glm::lookAt(position, position + glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, -1.0, 0.0)));

	return lightMatrices;
}

PointLight::~PointLight()
{
}

glm::vec3 PointLight::GetPosition()
{
	return position;
}
