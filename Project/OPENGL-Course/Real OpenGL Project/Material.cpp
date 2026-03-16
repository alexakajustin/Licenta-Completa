#include "Material.h"
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

Material::Material()
{
	specularIntensity = 0.0f;
	shininess = 0.0f;
	color = glm::vec3(1.0f);
	textureTiling = glm::vec2(1.0f);
	textureOffset = glm::vec2(0.0f);
}

Material::Material(GLfloat specularIntensity, GLfloat shininess, glm::vec3 color)
{
	this->specularIntensity = specularIntensity;
	this->shininess = shininess;
	this->color = color;
	this->textureTiling = glm::vec2(1.0f);
	this->textureOffset = glm::vec2(0.0f);
}

Material::~Material()
{
}

void Material::UseMaterial(GLint specularIntensityLocation, GLint shininessLocation, GLint colorLocation, GLint tilingLocation, GLint offsetLocation)
{
	glUniform1f(specularIntensityLocation, specularIntensity);
	glUniform1f(shininessLocation, shininess);
	glUniform3fv(colorLocation, 1, glm::value_ptr(color));
	glUniform2fv(tilingLocation, 1, glm::value_ptr(textureTiling));
	glUniform2fv(offsetLocation, 1, glm::value_ptr(textureOffset));
}

Material* Material::LoadFromFile(const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open()) return nullptr;

	float spec = 0.5f, shine = 32.0f;
	glm::vec3 col = glm::vec3(1.0f);
	glm::vec2 til = glm::vec2(1.0f);
	glm::vec2 off = glm::vec2(0.0f);
	std::string line;
	while (std::getline(file, line))
	{
		if (line.find("specular=") == 0) spec = std::stof(line.substr(9));
		else if (line.find("shininess=") == 0) shine = std::stof(line.substr(10));
		else if (line.find("color=") == 0) {
			std::string colorStr = line.substr(6);
			std::stringstream ss(colorStr);
			std::string r, g, b;
			if (std::getline(ss, r, ',') && std::getline(ss, g, ',') && std::getline(ss, b, ',')) {
				col = glm::vec3(std::stof(r), std::stof(g), std::stof(b));
			}
		}
		else if (line.find("tiling=") == 0) {
			std::string tilStr = line.substr(7);
			std::stringstream ss(tilStr);
			std::string x, y;
			if (std::getline(ss, x, ',') && std::getline(ss, y, ',')) {
				til = glm::vec2(std::stof(x), std::stof(y));
			}
		}
		else if (line.find("offset=") == 0) {
			std::string offStr = line.substr(7);
			std::stringstream ss(offStr);
			std::string x, y;
			if (std::getline(ss, x, ',') && std::getline(ss, y, ',')) {
				off = glm::vec2(std::stof(x), std::stof(y));
			}
		}
	}
	Material* mat = new Material(spec, shine, col);
	mat->SetTiling(til);
	mat->SetOffset(off);
	return mat;
}

bool Material::SaveToFile(const std::string& path) const
{
	std::ofstream file(path);
	if (!file.is_open()) return false;
	file << "specular=" << specularIntensity << "\n";
	file << "shininess=" << shininess << "\n";
	file << "color=" << color.r << "," << color.g << "," << color.b << "\n";
	file << "tiling=" << textureTiling.x << "," << textureTiling.y << "\n";
	file << "offset=" << textureOffset.x << "," << textureOffset.y << "\n";
	return true;
}
