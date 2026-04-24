#include "CascadedShadowMap.h"

CascadedShadowMap::CascadedShadowMap() : ShadowMap()
{
	cascadeCount = 0;
}

bool CascadedShadowMap::Init(GLuint width, GLuint height)
{
	return Init(width, height, 3); // Default to 3 cascades
}

bool CascadedShadowMap::Init(GLuint width, GLuint height, GLuint cascadeCount)
{
	this->shadowWidth = width;
	this->shadowHeight = height;
	this->cascadeCount = cascadeCount;

	glGenFramebuffers(1, &FBO);

	// Create Depth Texture Array
	glGenTextures(1, &shadowMap);
	glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, shadowWidth, shadowHeight, cascadeCount, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	
	float bColour[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bColour);

	// Create Color Texture Array (for semi-transparent shadows)
	glGenTextures(1, &shadowColorMap);
	glBindTexture(GL_TEXTURE_2D_ARRAY, shadowColorMap);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, shadowWidth, shadowHeight, cascadeCount, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	
	float cColour[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, cColour);

	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
	// We will attach layers dynamically in WriteLayer
	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowMap, 0);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowColorMap, 0);

	GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, drawBuffers);
	glReadBuffer(GL_NONE);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("Cascaded Shadow Map Framebuffer Error: %i\n", status);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return true;
}

void CascadedShadowMap::WriteLayer(GLuint layer)
{
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowMap, 0, layer);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowColorMap, 0, layer);
}

void CascadedShadowMap::Read(GLenum textureUnit)
{
	glActiveTexture(textureUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap);
}

void CascadedShadowMap::ReadColor(GLenum textureUnit)
{
	glActiveTexture(textureUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, shadowColorMap);
}

CascadedShadowMap::~CascadedShadowMap()
{
	// ShadowMap destructor will handle glDeleteTextures and glDeleteFramebuffers
}
