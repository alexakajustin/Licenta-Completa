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
	// The colour the light shines with this frame: its own colour (edited, saved) times a tint
	// the renderer sets - for the sun, what the atmosphere leaves of it (red and faint at the
	// horizon, gone below it)
	glm::vec3 GetRenderColour() const { return colour * renderTint; }
	// Its ambient (sky) light: the sky keeps glowing after the sun has set
	glm::vec3 GetAmbientRenderColour() const { return colour * ambientTint; }
	void SetRenderTint(const glm::vec3& tint, const glm::vec3& ambient) { renderTint = tint; ambientTint = ambient; }

	~Light();
	Light(Light&&) = default;
	Light& operator=(Light&&) = default;


protected:
	glm::vec3 colour;
	glm::vec3 renderTint = glm::vec3(1.0f);
	glm::vec3 ambientTint = glm::vec3(1.0f);
	GLfloat ambientIntensity;
	GLfloat diffuseIntensity;

	//proj matrix from the lights point of view
	glm::mat4 lightProj;

	std::unique_ptr<ShadowMap> shadowMap;
};