#include "Shader.h"

Shader::Shader()
{
	shaderID = 0;
	uniformModel = -1;
	uniformProjection = -1;
	uniformView = -1;
	uniformEyePosition = -1;
	uniformSpecularIntensity = -1;
	uniformShininess = -1;
	uniformTexture = -1;
	uniformNormalMap = -1;
	uniformUseNormalMap = -1;
	uniformDirectionalLightTransform = -1;
	uniformDirectionalShadowMap = -1;
	uniformOmniLightPos = -1;
	uniformFarPlane = -1;
	uniformTiling = -1;
	uniformOffset = -1;
	uniformPointLightCount = -1;
	uniformSpotLightCount = -1;
	pointLightCount = 0;
	spotLightCount = 0;

	for (int i = 0; i < 6; i++) uniformLightMatrices[i] = -1;
}

Shader::~Shader()
{
	ClearShader();
}

void Shader::CreateFromString(const char* vertexCode, const char* fragmentCode)
{
	CompileShader(vertexCode, fragmentCode);
}

void Shader::CreateFromFiles(const char* vertexLocation, const char* fragmentLocation)
{
	vertexPath = vertexLocation;
	fragmentPath = fragmentLocation;
	geometryPath = "";

	std::string vertexString = ReadFile(vertexLocation);
	std::string fragmentString = ReadFile(fragmentLocation);

	const char* vertexCode = vertexString.c_str();
	const char* fragmentCode = fragmentString.c_str();

	CompileShader(vertexCode, fragmentCode);
}

void Shader::CreateFromFiles(const char* vertexLocation, const char* geometryLocation, const char* fragmentLocation)
{
	vertexPath = vertexLocation;
	fragmentPath = fragmentLocation;
	geometryPath = geometryLocation;

	std::string vertexString = ReadFile(vertexLocation);
	std::string fragmentString = ReadFile(fragmentLocation);
	std::string geometryString = ReadFile(geometryLocation);

	const char* vertexCode = vertexString.c_str();
	const char* fragmentCode = fragmentString.c_str();
	const char* geometryCode = geometryString.c_str();

	CompileShader(vertexCode, geometryCode, fragmentCode);
}

void Shader::CreateFromFiles(const char* vertexLocation, const char* tcsLocation, const char* tesLocation, const char* fragmentLocation)
{
	vertexPath = vertexLocation;
	fragmentPath = fragmentLocation;
	tcsPath = tcsLocation;
	tesPath = tesLocation;
	geometryPath = "";
	hasTessellation = true;

	std::string vertexString = ReadFile(vertexLocation);
	std::string tcsString = ReadFile(tcsLocation);
	std::string tesString = ReadFile(tesLocation);
	std::string fragmentString = ReadFile(fragmentLocation);

	if (vertexString.empty() || tcsString.empty() || tesString.empty() || fragmentString.empty()) {
		printf("[Shader] Failed to read one or more tessellation shader files!\n");
		return;
	}

	CompileShader(vertexString.c_str(), tcsString.c_str(), tesString.c_str(), fragmentString.c_str());
}

void Shader::CreateComputeShader(const char* computePath)
{
	isComputeShader = true;
	vertexPath = computePath;
	fragmentPath = "";
	geometryPath = "";

	std::string computeString = ReadFile(computePath);
	if (computeString.empty()) {
		printf("[Shader] Failed to read compute shader: %s\n", computePath);
		return;
	}

	shaderID = glCreateProgram();
	if (!shaderID) {
		printf("[Shader] Error creating compute shader program!\n");
		return;
	}

	if (!AddShader(shaderID, computeString.c_str(), GL_COMPUTE_SHADER)) {
		ClearShader();
		return;
	}

	GLint result = 0;
	GLchar errorLog[1024] = { 0 };

	glLinkProgram(shaderID);
	glGetProgramiv(shaderID, GL_LINK_STATUS, &result);
	if (!result) {
		glGetProgramInfoLog(shaderID, sizeof(errorLog), NULL, errorLog);
		printf("[Shader] Compute shader link error: %s\n", errorLog);
		ClearShader();
		return;
	}

	printf("[Shader] Compute shader compiled successfully: %s (ID: %u)\n", computePath, shaderID);
}


void Shader::CompileShader(const char* vertexCode, const char* fragmentCode)
{
	// try to create shader program
	shaderID = glCreateProgram();

	if (!shaderID)
	{
		printf("Error creating shader program!");
		return;
	}

	// add the vertex and fragment shader to the shader program
	if (!AddShader(shaderID, vertexCode, GL_VERTEX_SHADER)) { ClearShader(); return; }
	if (!AddShader(shaderID, fragmentCode, GL_FRAGMENT_SHADER)) { ClearShader(); return; }

	CompileProgram();
	if (shaderID == 0) return;
}

void Shader::CompileShader(const char* vertexCode, const char* geometryCode, const char* fragmentCode)
{
	// try to create shader program
	shaderID = glCreateProgram();

	if (!shaderID)
	{
		printf("Error creating shader program!");
		return;
	}

	// add the vertex and fragment shader to the shader program
	if (!AddShader(shaderID, vertexCode, GL_VERTEX_SHADER)) { ClearShader(); return; }
	if (!AddShader(shaderID, geometryCode, GL_GEOMETRY_SHADER)) { ClearShader(); return; }
	if (!AddShader(shaderID, fragmentCode, GL_FRAGMENT_SHADER)) { ClearShader(); return; }

	CompileProgram();
	if (shaderID == 0) return;
}

void Shader::CompileShader(const char* vertexCode, const char* tcsCode, const char* tesCode, const char* fragmentCode)
{
	shaderID = glCreateProgram();

	if (!shaderID)
	{
		printf("Error creating tessellation shader program!");
		return;
	}

	if (!AddShader(shaderID, vertexCode, GL_VERTEX_SHADER)) { ClearShader(); return; }
	if (!AddShader(shaderID, tcsCode, GL_TESS_CONTROL_SHADER)) { ClearShader(); return; }
	if (!AddShader(shaderID, tesCode, GL_TESS_EVALUATION_SHADER)) { ClearShader(); return; }
	if (!AddShader(shaderID, fragmentCode, GL_FRAGMENT_SHADER)) { ClearShader(); return; }

	hasTessellation = true;
	CompileProgram();
	if (shaderID == 0) return;

	printf("[Shader] Tessellation shader compiled successfully (ID: %u)\n", shaderID);
}

bool Shader::AddShader(GLuint theProgram, const char* shaderCode, GLenum shaderType)
{
	GLuint theShader = glCreateShader(shaderType);

	const GLchar* theCode[1];
	theCode[0] = shaderCode;
	
	GLint codeLenght[1];
	codeLenght[0] = (GLint)strlen(shaderCode);

	glShaderSource(theShader, 1, theCode, codeLenght);

	glCompileShader(theShader);

	GLint result = 0;
	GLchar errorLog[1024] = { 0 };

	glGetShaderiv(theShader, GL_COMPILE_STATUS, &result);

	if (!result)
	{
		glGetShaderInfoLog(theShader, sizeof(errorLog), NULL, errorLog);
		printf("error at shader compile status: %s\n", errorLog);
		return false;
	}

	glAttachShader(theProgram, theShader);
	return true;
}

void Shader::Validate()
{
	GLint result = 0;
	GLchar errorLog[1024] = { 0 };

	glValidateProgram(shaderID);
	glGetProgramiv(shaderID, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shaderID, sizeof(errorLog), NULL, errorLog);
		printf("error at shader validation status: %s\n", errorLog);
		return;
	}
}


void Shader::CompileProgram()
{
	GLint result = 0;
	GLchar errorLog[1024] = { 0 };

	glLinkProgram(shaderID); // create the shader exe on the graphics card

	glGetProgramiv(shaderID, GL_LINK_STATUS, &result);

	if (!result)
	{
		glGetProgramInfoLog(shaderID, sizeof(errorLog), NULL, errorLog);
		printf("error at shader linking status: %s\n", errorLog);
		ClearShader();
		return;
	}

	uniformModel = glGetUniformLocation(shaderID, "model");
	uniformProjection = glGetUniformLocation(shaderID, "projection");
	uniformView = glGetUniformLocation(shaderID, "view");

	uniformDirectionalLight.uniformColour = glGetUniformLocation(shaderID, "directionalLight.base.colour");
	uniformDirectionalLight.uniformAmbientIntensity = glGetUniformLocation(shaderID, "directionalLight.base.ambientIntensity");
	uniformDirectionalLight.uniformDirection = glGetUniformLocation(shaderID, "directionalLight.direction");
	uniformDirectionalLight.uniformDiffuseIntensity = glGetUniformLocation(shaderID, "directionalLight.base.diffuseIntensity");

	uniformSpecularIntensity = glGetUniformLocation(shaderID, "material.specularIntensity");
	uniformShininess = glGetUniformLocation(shaderID, "material.shininess");
	uniformTiling = glGetUniformLocation(shaderID, "material.tiling");
	uniformOffset = glGetUniformLocation(shaderID, "material.offset");
	uniformEyePosition = glGetUniformLocation(shaderID, "eyePosition");

	uniformPointLightCount = glGetUniformLocation(shaderID, "pointLightCount");

	for (size_t i = 0; i < MAX_POINT_LIGHTS; i++)
	{
		char locBuff[100] = { '\0' };

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].base.colour", i);
		uniformPointLight[i].uniformColour = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].base.ambientIntensity", i);
		uniformPointLight[i].uniformAmbientIntensity = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].base.diffuseIntensity", i);
		uniformPointLight[i].uniformDiffuseIntensity = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].position", i);
		uniformPointLight[i].uniformPosition = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].constant", i);
		uniformPointLight[i].uniformConstant = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].linear", i);
		uniformPointLight[i].uniformLinear = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "pointLights[%zu].exponent", i);
		uniformPointLight[i].uniformExponent = glGetUniformLocation(shaderID, locBuff);
	}

	uniformSpotLightCount = glGetUniformLocation(shaderID, "spotLightCount");

	for (size_t i = 0; i < MAX_SPOT_LIGHTS; i++)
	{
		char locBuff[100] = { '\0' };

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.base.colour", i);
		uniformSpotLight[i].uniformColour = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.base.ambientIntensity", i);
		uniformSpotLight[i].uniformAmbientIntensity = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.base.diffuseIntensity", i);
		uniformSpotLight[i].uniformDiffuseIntensity = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.position", i);
		uniformSpotLight[i].uniformPosition = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.constant", i);
		uniformSpotLight[i].uniformConstant = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.linear", i);
		uniformSpotLight[i].uniformLinear = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].base.exponent", i);
		uniformSpotLight[i].uniformExponent = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].direction", i);
		uniformSpotLight[i].uniformDirection = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "spotLights[%zu].edge", i);
		uniformSpotLight[i].uniformEdge = glGetUniformLocation(shaderID, locBuff);
	}

	uniformTexture = glGetUniformLocation(shaderID, "theTexture");
	uniformNormalMap = glGetUniformLocation(shaderID, "normalMap");
	uniformUseNormalMap = glGetUniformLocation(shaderID, "useNormalMap");
	uniformDirectionalLightTransform = glGetUniformLocation(shaderID, "directionalLightTransform");
	uniformDirectionalShadowMap = glGetUniformLocation(shaderID, "directionalShadowMap");
	uniformDirectionalShadowColorMap = glGetUniformLocation(shaderID, "directionalShadowColorMap");

	uniformOmniLightPos = glGetUniformLocation(shaderID, "lightPos");
	uniformFarPlane = glGetUniformLocation(shaderID, "farPlane");

	for (size_t i = 0; i < 6; i++)
	{
		char locBuff[100] = { '\0' };

		snprintf(locBuff, sizeof(locBuff), "lightMatrices[%zu]", i);
		uniformLightMatrices[i] = glGetUniformLocation(shaderID, locBuff);
	}

	for (size_t i = 0; i < MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS; i++)
	{
		char locBuff[100] = { '\0' };

		snprintf(locBuff, sizeof(locBuff), "omniShadowMaps[%zd].shadowMap", i);
		uniformOmniShadowMap[i].shadowMap = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "omniShadowMaps[%zd].shadowColorMap", i);
		uniformOmniShadowMap[i].shadowColorMap = glGetUniformLocation(shaderID, locBuff);

		snprintf(locBuff, sizeof(locBuff), "omniShadowMaps[%zd].farPlane", i);
		uniformOmniShadowMap[i].farPlane = glGetUniformLocation(shaderID, locBuff);
	}

	// --- PREVENT NVIDIA SAMPLER TYPE COLLISION ---
	// Uninitialized samplers default to texture unit 0. 
	// If a samplerCube (shadowMap) and a sampler2D (theTexture) both default to unit 0,
	// NVIDIA drivers throw GL_INVALID_OPERATION and silently drop the entire draw call.
	// We MUST initialize them to safe, unique texture units at compile time.
	UseShader();
	
	if (uniformTexture != -1) glUniform1i(uniformTexture, 0);
	if (uniformNormalMap != -1) glUniform1i(uniformNormalMap, 1);
	if (uniformDirectionalShadowMap != -1) glUniform1i(uniformDirectionalShadowMap, 3);
	if (uniformDirectionalShadowColorMap != -1) glUniform1i(uniformDirectionalShadowColorMap, 20);

	for (size_t i = 0; i < MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS; i++)
	{
		if (uniformOmniShadowMap[i].shadowMap != -1) glUniform1i(uniformOmniShadowMap[i].shadowMap, 4 + (GLint)i);
		if (uniformOmniShadowMap[i].shadowColorMap != -1) glUniform1i(uniformOmniShadowMap[i].shadowColorMap, 21 + (GLint)i);
	}
	
	glUseProgram(0); // Unbind
	// ---------------------------------------------

	DiscoverUniforms();
}

void Shader::DiscoverUniforms()
{
	uniformProperties.clear();

	GLint count;
	glGetProgramiv(shaderID, GL_ACTIVE_UNIFORMS, &count);

	for (GLint i = 0; i < count; i++) {
		const GLsizei bufSize = 64;
		GLchar name[bufSize];
		GLsizei length;
		GLint size;
		GLenum type;
		glGetActiveUniform(shaderID, (GLuint)i, bufSize, &length, &size, &type, name);

		UniformProperty prop;
		prop.name = name;
		prop.location = glGetUniformLocation(shaderID, name);

		switch (type) {
		case GL_FLOAT:      prop.type = UniformType::Float; break;
		case GL_INT:        prop.type = UniformType::Int; break;
		case GL_FLOAT_VEC2: prop.type = UniformType::Vec2; break;
		case GL_FLOAT_VEC3: prop.type = UniformType::Vec3; break;
		case GL_FLOAT_VEC4: prop.type = UniformType::Vec4; break;
		case GL_FLOAT_MAT4: prop.type = UniformType::Mat4; break;
		case GL_SAMPLER_2D: prop.type = UniformType::Sampler2D; break;
		default:            prop.type = UniformType::Unknown; break;
		}

		// Filter out internal engine uniforms
		std::string n = prop.name;
		std::string n_lower = n;
		for (auto& c : n_lower) c = (char)tolower(c);

		if (n.find("[") != std::string::npos || // Skip array members
			n_lower.find("layer") != std::string::npos || 
			n_lower.find("light") != std::string::npos ||
			n_lower.find("shadow") != std::string::npos || 
			n_lower.find("transform") != std::string::npos ||
			n == "model" || n == "projection" || n == "view" || n == "eyePosition" || n == "time" ||
			n == "theTexture" || n == "normalMap" || n == "farPlane" ||
			n == "useDiffuseTexture" || n == "useInstancing" || n == "useNormalMap" ||
			n == "textureLayerCount")
			continue;

		if (prop.type == UniformType::Unknown || prop.type == UniformType::Mat4 || prop.type == UniformType::Sampler2D) 
			continue;

		uniformProperties[prop.name] = prop;
	}
}

void Shader::UseShader()
{
	glUseProgram(shaderID);
}

void Shader::ClearShader()
{
	if (shaderID != 0)
	{
		glDeleteProgram(shaderID);
		shaderID = 0;
	}

	uniformModel = -1;
	uniformProjection = -1;
	uniformView = -1;
	uniformEyePosition = -1;
	uniformSpecularIntensity = -1;
	uniformShininess = -1;
	uniformTexture = -1;
	uniformNormalMap = -1;
	uniformUseNormalMap = -1;
	uniformDirectionalLightTransform = -1;
	uniformDirectionalShadowMap = -1;
	uniformDirectionalShadowColorMap = -1;
	uniformOmniLightPos = -1;
	uniformFarPlane = -1;
	uniformTiling = -1;
	uniformOffset = -1;
	uniformPointLightCount = -1;
	uniformSpotLightCount = -1;
}

std::string Shader::ReadFile(const char* fileLocation)
{
	std::string content;
	std::ifstream fileStream(fileLocation, std::ios::in);

	if (!fileStream.is_open())
	{
		printf("Failed to read %s!", fileLocation);
		return "";
	}

	std::string line = "";
	while (!fileStream.eof())
	{
		std::getline(fileStream, line);
		content.append(line + "\n");
	}
	
	fileStream.close();

	return content;
}

GLint Shader::GetProjectionLocation()
{
	return uniformProjection;
}

GLint Shader::GetModelLocation()
{
	return uniformModel;
}

GLint Shader::GetViewLocation()
{
	return uniformView;
}

GLint Shader::GetAmbientIntensityLocation()
{
	return uniformDirectionalLight.uniformAmbientIntensity;
}

GLint Shader::GetAmbientColourLocation()
{
	return uniformDirectionalLight.uniformColour;
}

GLint Shader::GetDiffuseIntensityLocation()
{
	return uniformDirectionalLight.uniformDiffuseIntensity;
}

GLint Shader::GetDirectionLocation()
{
	return uniformDirectionalLight.uniformDirection;
}

GLint Shader::GetSpecularIntensityLocation()
{
	return uniformSpecularIntensity;
}

GLint Shader::GetShininessLocation()
{
	return uniformShininess;
}

GLint Shader::GetEyePositionLocation()
{
	return uniformEyePosition;
}

GLint Shader::getOmniLightPosLocation()
{
	return uniformOmniLightPos;
}

GLint Shader::getFarPlaneLocation()
{
	return uniformFarPlane;
}

void Shader::SetDirectionalLight(DirectionalLight* directionalLight)
{
	directionalLight->UseLight(uniformDirectionalLight.uniformAmbientIntensity, uniformDirectionalLight.uniformColour, uniformDirectionalLight.uniformDiffuseIntensity, uniformDirectionalLight.uniformDirection);

}

void Shader::SetPointLights(PointLight* pointLight, unsigned int lightCount, unsigned int textureUnit, unsigned int offset)
{
	if (lightCount > MAX_POINT_LIGHTS) lightCount = MAX_POINT_LIGHTS;

	glUniform1i(uniformPointLightCount, lightCount);

	for (size_t i = 0; i < lightCount; i++)
	{
		pointLight[i].UseLight(uniformPointLight[i].uniformAmbientIntensity, uniformPointLight[i].uniformColour,
			uniformPointLight[i].uniformDiffuseIntensity, uniformPointLight[i].uniformPosition,
			uniformPointLight[i].uniformConstant, uniformPointLight[i].uniformLinear, uniformPointLight[i].uniformExponent);

		pointLight[i].GetShadowMap()->Read(GL_TEXTURE0 + textureUnit + i);
		pointLight[i].GetShadowMap()->ReadColor(GL_TEXTURE0 + 21 + i); // Color Map on 21+
		glUniform1i(uniformOmniShadowMap[i + offset].shadowMap, textureUnit + i);
		glUniform1i(uniformOmniShadowMap[i + offset].shadowColorMap, 21 + i);
		glUniform1f(uniformOmniShadowMap[i + offset].farPlane, pointLight[i].GetFarPlane());

	}
}

void Shader::SetSpotLights(SpotLight* spotLight, unsigned int lightCount, unsigned int textureUnit, unsigned int offset)
{
	if (lightCount > MAX_SPOT_LIGHTS) lightCount = MAX_SPOT_LIGHTS;

	glUniform1i(uniformSpotLightCount, lightCount);

	for (size_t i = 0; i < lightCount; i++)
	{
		spotLight[i].UseLight(uniformSpotLight[i].uniformAmbientIntensity, uniformSpotLight[i].uniformColour,
			uniformSpotLight[i].uniformDiffuseIntensity, uniformSpotLight[i].uniformPosition, uniformSpotLight[i].uniformDirection,
			uniformSpotLight[i].uniformConstant, uniformSpotLight[i].uniformLinear, uniformSpotLight[i].uniformExponent, 
			uniformSpotLight[i].uniformEdge);

		spotLight[i].GetShadowMap()->Read(GL_TEXTURE0 + textureUnit + i);
		spotLight[i].GetShadowMap()->ReadColor(GL_TEXTURE0 + 25 + i); // Spot Color Map on 25+
		glUniform1i(uniformOmniShadowMap[i + offset].shadowMap, textureUnit + i);
		glUniform1i(uniformOmniShadowMap[i + offset].shadowColorMap, 25 + i);
		glUniform1f(uniformOmniShadowMap[i + offset].farPlane, spotLight[i].GetFarPlane());
	}
}

void Shader::SetTexture(GLuint textureUnit)
{
	glUniform1i(uniformTexture, textureUnit);
}

void Shader::SetNormalMap(GLuint textureUnit)
{
	glUniform1i(uniformNormalMap, textureUnit);
}

void Shader::SetUseNormalMap(bool useNormalMap)
{
	glUniform1i(uniformUseNormalMap, useNormalMap ? 1 : 0);
}

void Shader::SetDirectionalShadowMap(GLuint textureUnit)
{
	glUniform1i(uniformDirectionalShadowMap, textureUnit);
}

void Shader::SetDirectionalShadowColorMap(GLuint textureUnit)
{
	glUniform1i(uniformDirectionalShadowColorMap, textureUnit);
}

void Shader::SetDirectionalLightTransform(glm::mat4 lTransform)
{
	glUniformMatrix4fv(uniformDirectionalLightTransform, 1, GL_FALSE, glm::value_ptr(lTransform));
}

void Shader::SetLightMatrices(std::vector<glm::mat4> lightMatrices)
{
	for (size_t i = 0; i < 6; i++)
	{
		glUniformMatrix4fv(uniformLightMatrices[i], 1, GL_FALSE, glm::value_ptr(lightMatrices[i]));
	}
}
