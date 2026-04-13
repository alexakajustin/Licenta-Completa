#pragma once

#include <string>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Shader.h"

class Material
{
public:
	Material();
	Material(Shader* shader, glm::vec3 color = glm::vec3(1.0f));
	Material(float specular, float shininess, glm::vec3 color = glm::vec3(1.0f)); // Legacy constructor
	~Material();

	void SetShader(Shader* shader);
	Shader* GetShader() const { return shader; }

	void Bind();
	void UseMaterial(GLint specularIntensityLocation, GLint shininessLocation, GLint colorLocation, GLint tilingLocation, GLint offsetLocation); 
	void SetDefaults();

	// File I/O
	static Material* LoadFromFile(const std::string& path);
	bool SaveToFile(const std::string& path) const;

	// File I/O
	// Dynamic properties
	void SetFloat(const std::string& name, float val) { floats[name] = val; }
	float GetFloat(const std::string& name) const { return floats.count(name) ? floats.at(name) : 0.0f; }

	void SetVec3(const std::string& name, glm::vec3 val) { vec3s[name] = val; }
	glm::vec3 GetVec3(const std::string& name) const { return vec3s.count(name) ? vec3s.at(name) : glm::vec3(0.0f); }

	void SetVec4(const std::string& name, glm::vec4 val) { vec4s[name] = val; }
	glm::vec4 GetVec4(const std::string& name) const { return vec4s.count(name) ? vec4s.at(name) : glm::vec4(0.0f); }

	void SetVec2(const std::string& name, glm::vec2 val) { vec2s[name] = val; }
	glm::vec2 GetVec2(const std::string& name) const { return vec2s.count(name) ? vec2s.at(name) : glm::vec2(0.0f); }

	// Keep these for backward compatibility/helper access
	glm::vec4 GetColor() const { return GetVec4("material.baseColor"); }
	void SetColor(glm::vec4 val) { SetVec4("material.baseColor", val); }
	void SetColor(glm::vec3 val) { SetVec4("material.baseColor", glm::vec4(val, 1.0f)); }
	glm::vec3 GetColorRGB() const { glm::vec4 c = GetColor(); return glm::vec3(c.r, c.g, c.b); }
	float GetAlpha() const { return GetColor().a; }
	void SetAlpha(float a) { glm::vec4 c = GetColor(); c.a = a; SetColor(c); }

	float GetSpecularIntensity() const { return GetFloat("material.specularIntensity"); }
	void SetSpecularIntensity(float val) { SetFloat("material.specularIntensity", val); }

	float GetShininess() const { return GetFloat("material.shininess"); }
	void SetShininess(float val) { SetFloat("material.shininess", val); }

	glm::vec2 GetTiling() const { return GetVec2("material.tiling"); }
	void SetTiling(glm::vec2 val) { SetVec2("material.tiling", val); }

	glm::vec2 GetOffset() const { return GetVec2("material.offset"); }
	void SetOffset(glm::vec2 val) { SetVec2("material.offset", val); }

	const std::map<std::string, float>& GetFloats() const { return floats; }
	const std::map<std::string, glm::vec2>& GetVec2s() const { return vec2s; }
	const std::map<std::string, glm::vec3>& GetVec3s() const { return vec3s; }
	const std::map<std::string, glm::vec4>& GetVec4s() const { return vec4s; }

private:
	Shader* shader;

	std::map<std::string, float> floats;
	std::map<std::string, glm::vec2> vec2s;
	std::map<std::string, glm::vec3> vec3s;
	std::map<std::string, glm::vec4> vec4s;

	mutable std::map<std::string, GLint> uniformLocations;
};

