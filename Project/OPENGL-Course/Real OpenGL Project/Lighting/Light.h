#pragma once

#include <GL\glew.h>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include "Rendering/ShadowMap.h"

#include <vector>
#include <memory>

class Light
{
public:
	Light();

	Light(GLfloat shadowWidth, GLfloat shadowHeight,
		GLfloat red, GLfloat green, GLfloat blue, GLfloat ambientIntensity, GLfloat diffuseIntensity);

	ShadowMap* GetShadowMap() { return shadowMap.get(); }

	// Getters for editing
	glm::vec3* GetColourPtr() { return &colour; }
	GLfloat* GetAmbientIntensityPtr() { return &ambientIntensity; }
	GLfloat* GetDiffuseIntensityPtr() { return &diffuseIntensity; }

	~Light();
	Light(Light&&) = default;
	Light& operator=(Light&&) = default;


protected:
	glm::vec3 colour;
	GLfloat ambientIntensity;
	GLfloat diffuseIntensity;

	//proj matrix from the lights point of view
	glm::mat4 lightProj;

	std::unique_ptr<ShadowMap> shadowMap;
};