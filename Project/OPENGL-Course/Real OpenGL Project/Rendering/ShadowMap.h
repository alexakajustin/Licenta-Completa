#pragma once

#include <stdio.h>
#include <GL\glew.h>

class ShadowMap
{
public:
	ShadowMap();

	virtual bool Init(GLuint width, GLuint height);
	
	//first pass, writing to shadowmap
	virtual void Write();

	//second pass, use it as texture
	virtual void Read(GLenum textureUnit);
	virtual void ReadColor(GLenum textureUnit);

	GLuint GetShadowWidth() { return shadowWidth; }
	GLuint GetShadowHeight() { return shadowHeight; }

	GLuint GetFBO() const { return FBO; }
	GLuint GetTextureID() const { return shadowMap; }
	GLuint GetColorTextureID() const { return shadowColorMap; }

	~ShadowMap();
protected:
	//these are ids
	GLuint FBO, shadowMap, shadowColorMap;
	GLuint shadowWidth, shadowHeight;
};

