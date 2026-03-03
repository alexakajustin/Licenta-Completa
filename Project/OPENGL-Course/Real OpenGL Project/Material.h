#pragma once

#include <GL/glew.h>
#include <string>
#include <glm/glm.hpp>

class Material
{
public:
	Material();
	Material(GLfloat specularIntensity, GLfloat shininess, glm::vec3 color = glm::vec3(1.0f));
	~Material();

	void UseMaterial(GLint specularIntensityLocation, GLint shininessLocation, GLint colorLocation);

	// File I/O
	static Material* LoadFromFile(const std::string& path);
	bool SaveToFile(const std::string& path) const;

	GLfloat GetSpecularIntensity() const { return specularIntensity; }
	void SetSpecularIntensity(GLfloat val) { specularIntensity = val; }
	GLfloat GetShininess() const { return shininess; }
	void SetShininess(GLfloat val) { shininess = val; }
	glm::vec3 GetColor() const { return color; }
	void SetColor(glm::vec3 val) { color = val; }

private:
	GLfloat specularIntensity;
	GLfloat shininess;	
	glm::vec3 color;
};

