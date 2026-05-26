#pragma once

#include "Rendering/ShadowMap.h"
#include <vector>

class CascadedShadowMap : public ShadowMap
{
public:
	CascadedShadowMap();

	virtual bool Init(GLuint width, GLuint height) override;
	bool Init(GLuint width, GLuint height, GLuint cascadeCount);

	// first pass, writing to a specific layer of the shadowmap array
	void WriteLayer(GLuint layer);

	// second pass, use it as texture array
	virtual void Read(GLenum textureUnit) override;
	virtual void ReadColor(GLenum textureUnit) override;

	GLuint GetCascadeCount() const { return cascadeCount; }

	~CascadedShadowMap();

private:
	GLuint cascadeCount;
};
