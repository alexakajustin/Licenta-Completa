#pragma once

#include <stdio.h>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>

#include <GL/glew.h>

#include <glm\gtc\type_ptr.hpp>
#include <glm\glm.hpp>

#include "CommonValues.h"

#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"

class Shader
{
public:
	enum class UniformType {
		Float,
		Int,
		Vec2,
		Vec3,
		Vec4,
		Mat4,
		Sampler2D,
		Unknown
	};

	struct UniformProperty {
		std::string name;
		UniformType type;
		GLint location;
	};

	Shader();
	~Shader();

	void CreateFromString(const char* vertexCode, const char* fragmentCode);
	void CreateFromFiles(const char* vertexLocation, const char* fragmentLocation);
	void CreateFromFiles(const char* vertexLocation, const char* geometryLocation, const char* fragmentLocation);
	void CreateComputeShader(const char* computePath);

	void Validate();

	std::string ReadFile(const char* fileLocation);

	GLint GetProjectionLocation();
	GLint GetModelLocation();
	GLint GetViewLocation();
	GLint GetAmbientIntensityLocation();
	GLint GetAmbientColourLocation();
	GLint GetDiffuseIntensityLocation();
	GLint GetDirectionLocation();
	GLint GetSpecularIntensityLocation();
	GLint GetShininessLocation();
	GLint GetEyePositionLocation();
	GLint getOmniLightPosLocation();
	GLint getFarPlaneLocation();
	GLint GetTilingLocation() { return uniformTiling; }
	GLint GetOffsetLocation() { return uniformOffset; }

	void SetDirectionalLight(DirectionalLight* directionalLight);
	void SetPointLights(PointLight* pointLight, unsigned int lightCount, unsigned int textureUnit, unsigned int offset);
	void SetSpotLights(SpotLight* spotLight, unsigned int lightCount, unsigned int textureUnit, unsigned int offset);

	void SetTexture(GLuint textureUnit);
	void SetNormalMap(GLuint textureUnit);
	void SetUseNormalMap(bool useNormalMap);
	void SetDirectionalShadowMap(GLuint textureUnit);
	void SetDirectionalShadowColorMap(GLuint textureUnit);
	void SetDirectionalLightTransform(glm::mat4 lTransform);
	void SetLightMatrices(std::vector<glm::mat4> lightMatrices);


	void UseShader();
	void ClearShader();
	GLuint GetShaderID() { return shaderID; }

	const std::map<std::string, UniformProperty>& GetUniformProperties() const { return uniformProperties; }

	const std::string& GetVertexPath() const { return vertexPath; }
	const std::string& GetFragmentPath() const { return fragmentPath; }

	bool isComputeShader = false;

private:
	std::string vertexPath;
	std::string fragmentPath;
	std::string geometryPath;

	std::map<std::string, UniformProperty> uniformProperties;
	void DiscoverUniforms();
	int pointLightCount;
	int spotLightCount;
	GLuint shaderID;
	GLint uniformProjection, uniformModel, uniformView, uniformEyePosition,
		uniformSpecularIntensity, uniformShininess,
		uniformTexture, uniformNormalMap, uniformUseNormalMap,
		uniformDirectionalLightTransform, uniformDirectionalShadowMap, uniformDirectionalShadowColorMap,
		uniformOmniLightPos, uniformFarPlane,
		uniformTiling, uniformOffset;

	GLint uniformLightMatrices[6];

	struct {
		GLint uniformColour;
		GLint uniformAmbientIntensity;
		GLint uniformDiffuseIntensity;

		GLint uniformDirection;
	} uniformDirectionalLight;

	GLint uniformPointLightCount;

	struct {
		GLint uniformColour;
		GLint uniformAmbientIntensity;
		GLint uniformDiffuseIntensity;

		GLint uniformPosition;
		GLint uniformConstant;
		GLint uniformLinear;
		GLint uniformExponent;
	} uniformPointLight[MAX_POINT_LIGHTS];

	GLint uniformSpotLightCount;

	struct {
		GLint uniformColour;
		GLint uniformAmbientIntensity;
		GLint uniformDiffuseIntensity;

		GLint uniformPosition;
		GLint uniformConstant;
		GLint uniformLinear;
		GLint uniformExponent;

		GLint uniformDirection;
		GLint uniformEdge;
	} uniformSpotLight[MAX_SPOT_LIGHTS];

	struct {
		GLint shadowMap;
		GLint shadowColorMap;
		GLint farPlane;
	} uniformOmniShadowMap[MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS];
	
	void CompileShader(const char* vertexCode, const char* fragmentCode);
	void CompileShader(const char* vertexCode, const char* geometryCode, const char* fragmentCode);
	bool AddShader(GLuint theProgram, const char* shaderCode, GLenum shaderType);
	void CompileProgram();
};

