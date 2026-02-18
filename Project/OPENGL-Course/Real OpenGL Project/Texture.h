#pragma once
#include <GL/glew.h>

#include <string.h>

#include "CommonValues.h"

class Texture
{
public:
	Texture();
	Texture(const char* fileLoc);

	bool LoadTexture();
	bool LoadTextureA(); // texture with alpha
	void UseTexture();
	void UseNormalMap();
	void ClearTexture();

	GLuint GetTextureID() const { return textureID; }
	void SetTextureID(GLuint id) { textureID = id; }
	const char* GetFileLocation() const { return fileLocation; }

	~Texture();
private:
	// id on graphics card
	GLuint textureID;
	int width, height, bitDepth;
	char* fileLocation;
};

