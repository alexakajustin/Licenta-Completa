#pragma once

#include <vector>
#include <string>

#include <GL\glew.h>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>
#include <glm\gtc\type_ptr.hpp>

#include "CommonValues.h"

#include "Rendering/Mesh.h"
#include "Rendering/Shader.h"

class Skybox
{
public:
	Skybox();

	Skybox(std::vector<std::string> faceLocations);
	
	void DrawSkybox(glm::mat4 viewMatrix, glm::mat4 projectionMatrix);

	GLuint GetTextureID() const { return textureId; }

	~Skybox();
private:
	Mesh* skyMesh;
	Shader* skyShader;

	GLuint textureId;
	GLuint uniformProjection, uniformView;
};

