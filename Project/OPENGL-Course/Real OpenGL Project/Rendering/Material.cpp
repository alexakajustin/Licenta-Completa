#include "Rendering/Material.h"
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#include "Rendering/Renderer.h" // To access default shaders if needed

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
	floats["material.reflectivity"] = 0.0f;
	vec4s["material.baseColor"] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	vec2s["material.tiling"] = glm::vec2(1.0f);
	vec2s["material.offset"] = glm::vec2(0.0f);
}

void Material::SetShader(Shader* shader)
{
	this->shader = shader;
	needsDefaultSync = true;
	uniformLocations.clear();

	if (shader) {
		// No hardcoded defaults here anymore.
	}
}

void Material::SetTextureParam(const std::string& name, const std::string& path)
{
	texturePaths[name] = path;
	// Load the texture if not already loaded
	if (textures.find(name) == textures.end() || textures[name] == nullptr) {
		Texture* tex = new Texture(path.c_str());
		if (tex->LoadTextureA()) {
			textures[name] = tex;
			printf("[Material] Loaded texture param '%s' from '%s'\n", name.c_str(), path.c_str());
		} else {
			delete tex;
		}
	}
}

void Material::SetTexture(const std::string& name, Texture* texture)
{
	if (texture) {
		textures[name] = texture;
		texturePaths[name] = texture->GetFileLocation();
	}
}

void Material::Bind(GLuint overrideProgram)
{
	GLuint programID = overrideProgram;
	if (programID == 0) {
		if (!shader) return;
		programID = shader->GetShaderID();
	}

	if (programID == 0) return;

	glUseProgram(programID);

	if (needsDefaultSync) {
		InitializeDefaultsFromShader();
		needsDefaultSync = false;
	}

	auto GetLoc = [&](const std::string& name) {
		if (overrideProgram == 0) {
			if (uniformLocations.count(name)) return uniformLocations[name];
			GLint loc = glGetUniformLocation(programID, name.c_str());
			uniformLocations[name] = loc;
			return loc;
		} else {
			auto& cache = overrideUniformLocations[overrideProgram];
			if (cache.count(name)) return cache[name];
			GLint loc = glGetUniformLocation(overrideProgram, name.c_str());
			cache[name] = loc;
			return loc;
		}
	};

	for (auto const& [name, val] : floats) {
		GLint loc = GetLoc(name);
		if (loc != -1) glUniform1f(loc, val);
	}

	for (auto const& [name, val] : ints) {
		GLint loc = GetLoc(name);
		if (loc != -1) glUniform1i(loc, val);
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
		if (loc != -1) {
			while(glGetError() != GL_NO_ERROR); // clear previous errors
			glUniform4fv(loc, 1, glm::value_ptr(val));
			GLenum err = glGetError();
			if (err != GL_NO_ERROR) {
				std::cout << "[ERROR] glUniform4fv failed for vec4 uniform: " << name << " at location: " << loc << "\n";
			}
		}
	}

	// Bind texture parameters to high texture units (10+)
	int texUnit = 10;
	for (auto const& [name, tex] : textures) {
		if (!tex) continue;
		GLint loc = GetLoc(name);
		if (loc != -1) {
			glActiveTexture(GL_TEXTURE0 + texUnit);
			glBindTexture(GL_TEXTURE_2D, tex->GetTextureID());
			glUniform1i(loc, texUnit);
			texUnit++;
		}
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
	std::string vPath, fPath;

	while (std::getline(file, line))
	{
		if (line.empty()) continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string key = line.substr(0, eq);
		std::string valStr = line.substr(eq + 1);

		if (key == "shader_vert") vPath = valStr;
		else if (key == "shader_frag") fPath = valStr;
		else if (key.rfind("texture_", 0) == 0) {
			// texture_XXX=path/to/file.png -> loads texture and stores as "material_XXX"
			std::string uniformName = "material_" + key.substr(8); // strip "texture_" prefix
			mat->SetTextureParam(uniformName, valStr);
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
			// If it's a whole number, store as int? 
			// For now, let's keep it simple: if it contains a '.', it's a float.
			if (valStr.find('.') != std::string::npos) {
				mat->SetFloat(key, std::stof(valStr));
			} else {
				try {
					mat->SetInt(key, std::stoi(valStr));
				} catch (...) {
					mat->SetFloat(key, std::stof(valStr));
				}
			}
		}
	}

	if (!vPath.empty() && !fPath.empty())
	{
		Shader* s = new Shader();
		s->CreateFromFiles(vPath.c_str(), fPath.c_str());
		mat->SetShader(s);
	}

	mat->SetPath(path);
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
	for (auto const& [name, val] : ints)   file << name << "=" << val << "\n";
	for (auto const& [name, val] : vec2s)  file << name << "=" << val.x << "," << val.y << "\n";
	for (auto const& [name, val] : vec3s)  file << name << "=" << val.x << "," << val.y << "," << val.z << "\n";
	for (auto const& [name, val] : vec4s)  file << name << "=" << val.x << "," << val.y << "," << val.z << "," << val.w << "\n";
	for (auto const& [name, path] : texturePaths) {
		// Convert back: "material_XXX" -> "texture_XXX"
		std::string key = name;
		if (key.rfind("material_", 0) == 0) key = "texture_" + key.substr(9);
		file << key << "=" << path << "\n";
	}

	return true;
}

void Material::InitializeDefaultsFromShader()
{
	if (!shader || shader->GetShaderID() == 0) return;

	GLuint program = shader->GetShaderID();
	GLint currentProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
	glUseProgram(program);
	
	for (auto const& [name, prop] : shader->GetUniformProperties()) {
		if (prop.type == Shader::UniformType::Float) {
			if (floats.find(name) == floats.end()) {
				float val;
				glGetUniformfv(program, prop.location, &val);
				floats[name] = val;
			}
		}
		else if (prop.type == Shader::UniformType::Int) {
			if (ints.find(name) == ints.end()) {
				int val;
				glGetUniformiv(program, prop.location, &val);
				ints[name] = val;
			}
		}
		else if (prop.type == Shader::UniformType::Vec2) {
			if (vec2s.find(name) == vec2s.end()) {
				glm::vec2 val;
				glGetUniformfv(program, prop.location, glm::value_ptr(val));
				vec2s[name] = val;
			}
		}
		else if (prop.type == Shader::UniformType::Vec3) {
			if (vec3s.find(name) == vec3s.end()) {
				glm::vec3 val;
				glGetUniformfv(program, prop.location, glm::value_ptr(val));
				vec3s[name] = val;
			}
		}
		else if (prop.type == Shader::UniformType::Vec4) {
			if (vec4s.find(name) == vec4s.end()) {
				glm::vec4 val;
				glGetUniformfv(program, prop.location, glm::value_ptr(val));
				vec4s[name] = val;
			}
		}
	}
	glUseProgram(currentProgram);
}
