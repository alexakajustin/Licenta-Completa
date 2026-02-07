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

	GLuint GetShadowWidth() { return shadowWidth; }
	GLuint GetShadowHeight() { return shadowHeight; }

	~ShadowMap();
protected:
	//these are ids
	GLuint FBO, shadowMap;
	GLuint shadowWidth, shadowHeight;
};

