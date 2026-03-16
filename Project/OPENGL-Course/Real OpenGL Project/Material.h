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

	void UseMaterial(GLint specularIntensityLocation, GLint shininessLocation, GLint colorLocation, 
		GLint tilingLocation, GLint offsetLocation);

	// File I/O
	static Material* LoadFromFile(const std::string& path);
	bool SaveToFile(const std::string& path) const;

	GLfloat GetSpecularIntensity() const { return specularIntensity; }
	void SetSpecularIntensity(GLfloat val) { specularIntensity = val; }
	GLfloat GetShininess() const { return shininess; }
	void SetShininess(GLfloat val) { shininess = val; }
	glm::vec3 GetColor() const { return color; }
	void SetColor(glm::vec3 val) { color = val; }

	glm::vec2 GetTiling() const { return textureTiling; }
	void SetTiling(glm::vec2 val) { textureTiling = val; }
	glm::vec2 GetOffset() const { return textureOffset; }
	void SetOffset(glm::vec2 val) { textureOffset = val; }

private:
	GLfloat specularIntensity;
	GLfloat shininess;	
	glm::vec3 color;
	glm::vec2 textureTiling;
	glm::vec2 textureOffset;
};

