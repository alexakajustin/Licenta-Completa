#pragma once
#include "ShadowMap.h"
class OmniShadowMap :
    public ShadowMap
{
public:
    OmniShadowMap();

	bool Init(GLuint width, GLuint height);

	//first pass, writing to omni shadowmap
	void Write();

	//second pass, use it as texture cube
	void Read(GLenum textureUnit);

    ~OmniShadowMap();
private:

};

