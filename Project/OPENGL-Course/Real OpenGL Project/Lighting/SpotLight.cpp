#include "Lighting/SpotLight.h"

SpotLight::SpotLight() : PointLight()
{
	direction = glm::vec3(0.0f, -1.0f, 0.0f);
	edge = 0.0f;
	procEdge = cosf(glm::radians(this->edge));
}

SpotLight::SpotLight(GLfloat shadowWidth, GLfloat shadowHeight,
	GLfloat near, GLfloat far, 
	GLfloat red, GLfloat green, GLfloat blue,
	GLfloat ambientIntensity, GLfloat diffuseIntensity,
	GLfloat xPosition, GLfloat yPosition, GLfloat zPosition,
	GLfloat xDir, GLfloat yDir, GLfloat zDir,
	GLfloat constant, GLfloat linear, GLfloat exponent, 
	GLfloat edge) : PointLight(shadowWidth, shadowHeight, near, far, red, green, blue, ambientIntensity, diffuseIntensity, xPosition, yPosition, zPosition, constant, linear, exponent)
{
	direction = glm::normalize(glm::vec3(xDir, yDir, zDir));

	this->edge = edge;
	procEdge = cosf(glm::radians(this->edge));
}

void SpotLight::UseLight(GLuint ambientIntensityLocation, GLuint ambientColourLocation, 
	GLuint diffuseIntensityLocation, GLuint positionLocation, GLuint directionLocation,
	GLuint constantLocation, GLuint linearLocation, GLuint exponentLocation,
	GLuint edgeLocation)
{
	if (ambientColourLocation != (GLuint)-1) glUniform3f(ambientColourLocation, colour.x, colour.y, colour.z);
	if (ambientIntensityLocation != (GLuint)-1) glUniform1f(ambientIntensityLocation, ambientIntensity);
	if (diffuseIntensityLocation != (GLuint)-1) glUniform1f(diffuseIntensityLocation, diffuseIntensity);

	if (positionLocation != (GLuint)-1) glUniform3f(positionLocation, position.x, position.y, position.z);

	if (constantLocation != (GLuint)-1) glUniform1f(constantLocation, constant);
	if (linearLocation != (GLuint)-1) glUniform1f(linearLocation, linear);
	if (exponentLocation != (GLuint)-1) glUniform1f(exponentLocation, exponent);

	if (directionLocation != (GLuint)-1) glUniform3f(directionLocation, direction.x, direction.y, direction.z);
	if (edgeLocation != (GLuint)-1) glUniform1f(edgeLocation, cosf(glm::radians(edge)));
}

void SpotLight::SetFlash(glm::vec3 pos, glm::vec3 dir)
{
	position = pos;
	direction = dir;
}

SpotLight::~SpotLight()
{
}
