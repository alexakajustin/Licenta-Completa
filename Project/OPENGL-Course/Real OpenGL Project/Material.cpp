#include "Material.h"
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#include "Renderer.h" // To access default shaders if needed

Material::Material()
{
	shader = nullptr;
	SetDefaults();
}

Material::Material(Shader* shader, glm::vec3 color)
{
	this->shader = shader;
	SetDefaults();
	SetColor(color);
}

Material::Material(float specular, float shininess, glm::vec3 color)
{
	this->shader = nullptr;
	SetDefaults();
	SetSpecularIntensity(specular);
	SetShininess(shininess);
	SetColor(color);
}

Material::~Material()
{
}

void Material::SetDefaults()
{
	floats["material.specularIntensity"] = 0.5f;
	floats["material.shininess"] = 32.0f;
	floats["material.sssScale"] = 100.0f; // Scale to convert light space depth diff to world/local space
	floats["material.sssDistortion"] = 0.2f; // SSS normal distortion
	vec4s["material.baseColor"] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	vec2s["material.tiling"] = glm::vec2(1.0f);
	vec2s["material.offset"] = glm::vec2(0.0f);
}

void Material::SetShader(Shader* shader)
{
	this->shader = shader;
	uniformLocations.clear();
}

void Material::Bind()
{
	if (!shader) return;

	auto GetLoc = [&](const std::string& name) {
		if (uniformLocations.count(name)) return uniformLocations[name];
		GLint loc = glGetUniformLocation(shader->GetShaderID(), name.c_str());
		uniformLocations[name] = loc;
		return loc;
	};

	for (auto const& [name, val] : floats) {
		GLint loc = GetLoc(name);
		if (loc != -1) glUniform1f(loc, val);
	}

	for (auto const& [name, val] : vec2s) {
		GLint loc = GetLoc(name);
		if (loc != -1) glUniform2fv(loc, 1, glm::value_ptr(val));
	}

	for (auto const& [name, val] : vec3s) {
		GLint loc = GetLoc(name);
		if (loc != -1) glUniform3fv(loc, 1, glm::value_ptr(val));
	}

	for (auto const& [name, val] : vec4s) {
		GLint loc = GetLoc(name);
		if (loc != -1) glUniform4fv(loc, 1, glm::value_ptr(val));
	}
}

void Material::UseMaterial(GLint specularIntensityLocation, GLint shininessLocation, GLint colorLocation, GLint tilingLocation, GLint offsetLocation)
{
	if (specularIntensityLocation != -1) glUniform1f(specularIntensityLocation, GetSpecularIntensity());
	if (shininessLocation != -1) glUniform1f(shininessLocation, GetShininess());
	if (colorLocation != -1) glUniform4fv(colorLocation, 1, glm::value_ptr(GetColor()));
	if (tilingLocation != -1) glUniform2fv(tilingLocation, 1, glm::value_ptr(GetTiling()));
	if (offsetLocation != -1) glUniform2fv(offsetLocation, 1, glm::value_ptr(GetOffset()));
}

Material* Material::LoadFromFile(const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open()) return nullptr;

	Material* mat = new Material();
	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty()) continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string key = line.substr(0, eq);
		std::string valStr = line.substr(eq + 1);

		if (key == "shader_vert") {
			// We can't easily load the shader here without a manager, 
			// but we'll store the path or let the caller handle it.
			// For now, these will be handled by the SceneSerializer
		}
		else if (valStr.find(',') != std::string::npos) {
			// Probable vec2, vec3, or vec4
			std::stringstream ss(valStr);
			std::vector<float> values;
			std::string temp;
			while (std::getline(ss, temp, ',')) values.push_back(std::stof(temp));

			if (values.size() == 2) mat->SetVec2(key, glm::vec2(values[0], values[1]));
			else if (values.size() == 3) mat->SetVec3(key, glm::vec3(values[0], values[1], values[2]));
			else if (values.size() == 4) mat->SetVec4(key, glm::vec4(values[0], values[1], values[2], values[3]));
		}
		else {
			mat->SetFloat(key, std::stof(valStr));
		}
	}
	return mat;
}

bool Material::SaveToFile(const std::string& path) const
{
	std::ofstream file(path);
	if (!file.is_open()) return false;

	if (shader) {
		file << "shader_vert=" << shader->GetVertexPath() << "\n";
		file << "shader_frag=" << shader->GetFragmentPath() << "\n";
	}

	for (auto const& [name, val] : floats) file << name << "=" << val << "\n";
	for (auto const& [name, val] : vec2s)  file << name << "=" << val.x << "," << val.y << "\n";
	for (auto const& [name, val] : vec3s)  file << name << "=" << val.x << "," << val.y << "," << val.z << "\n";
	for (auto const& [name, val] : vec4s)  file << name << "=" << val.x << "," << val.y << "," << val.z << "," << val.w << "\n";

	return true;
}
