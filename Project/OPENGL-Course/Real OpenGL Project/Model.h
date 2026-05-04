#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>

#include "Mesh.h"
#include "Texture.h"
#include "Material.h"
#include "MeshData.h"

class Model
{
public:
	Model();
	
	// Split loading for multithreading
	void LoadModelCPU(const std::string& fileName);
	void LoadModelGPU();

	bool IsReady() const { return isGPUReady; }
	bool IsCPUReady() const { return isCPUReady; }
	bool IsFailed() const { return loadFailed; }
	
	void RenderModel(GLuint uniformUseNormalMap, GLuint uniformUseDiffuseTexture, GLuint uniformNormalMapSampler, GLuint uniformDiffuseTextureSampler);
	void RenderModelGeometryOnly(); // Render meshes without binding model textures (for overrides)
	void ClearModel();

	~Model();

	glm::vec3 GetMinBound() const { return minBound; }
	glm::vec3 GetMaxBound() const { return maxBound; }
	bool HasTextures() const {
		for (auto* tex : textureList) if (tex != nullptr) return true;
		return false;
	}

	const std::vector<MeshData>& GetMeshDataList() const { return meshDataList; }
	const std::vector<std::string>& GetMeshNames() const { return meshNames; }
	size_t GetMeshCount() const { return meshList.size(); }
	Mesh* GetMesh(size_t index) const { return index < meshList.size() ? meshList[index] : nullptr; }
	unsigned int GetMaterialIndex(size_t index) const { return index < meshToTex.size() ? meshToTex[index] : 0; }
	Texture* GetTexture(unsigned int matIndex) const { return matIndex < textureList.size() ? textureList[matIndex] : nullptr; }
	Texture* GetNormalMap(unsigned int matIndex) const { return matIndex < normalMapList.size() ? normalMapList[matIndex] : nullptr; }
	Material* GetMaterialInstance(unsigned int matIndex) const { return matIndex < materialList.size() ? materialList[matIndex] : nullptr; }

private:
	// scene contains all data, node is just one part of that list of data
	void LoadNode(aiNode* node, const aiScene* scene);
	void LoadMesh(aiMesh* mesh, const aiScene* scene);
	void LoadMaterials(const aiScene* scene);

	std::vector <Mesh*> meshList;
	std::vector <std::string> meshNames;
	std::vector <Texture*> textureList;
	std::vector <Texture*> normalMapList;
	std::vector <Material*> materialList;
	std::vector<unsigned int> meshToTex;
	std::vector<MeshData> meshDataList;

	bool isCPUReady = false;
	bool isGPUReady = false;
	bool loadFailed = false;
	
	struct IntermediateMeshData {
		std::vector<GLfloat> vertices;
		std::vector<unsigned int> indices;
		unsigned int materialIndex;
		std::string name;
	};
	std::vector<IntermediateMeshData> intermediateMeshes;

	glm::vec3 minBound = glm::vec3(1e10);
	glm::vec3 maxBound = glm::vec3(-1e10);
	std::string filePath;
};

