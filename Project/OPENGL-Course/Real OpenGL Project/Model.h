#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

#include "Mesh.h"
#include "Texture.h"

class Model
{
public:
	Model();
	
	void LoadModel(const std::string& fileName);
	void RenderModel(GLuint uniformUseNormalMap);
	void ClearModel();

	~Model();

	glm::vec3 GetMinBound() const { return minBound; }
	glm::vec3 GetMaxBound() const { return maxBound; }
	bool HasTextures() const {
		for (auto* tex : textureList) if (tex != nullptr) return true;
		return false;
	}

private:
	// scene contains all data, node is just one part of that list of data
	void LoadNode(aiNode* node, const aiScene* scene);
	void LoadMesh(aiMesh* mesh, const aiScene* scene);
	void LoadMaterials(const aiScene* scene);

	std::vector <Mesh*> meshList;
	std::vector <Texture*> textureList;
	std::vector <Texture*> normalMapList;
	std::vector<unsigned int> meshToTex;

	glm::vec3 minBound = glm::vec3(1e10);
	glm::vec3 maxBound = glm::vec3(-1e10);
};

