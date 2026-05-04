#include "Model.h"
#include <algorithm>

#include <assimp/Exporter.hpp>
#include <assimp/ProgressHandler.hpp>
#include <filesystem>
#include <future>

class ModelProgressHandler : public Assimp::ProgressHandler {
public:
	Model* model;
	ModelProgressHandler(Model* m) : model(m) {}
	virtual bool Update(float percentage) override {
		if (percentage >= 0.0f) {
			// Assimp takes 0.0 to 0.5 of the total progress
			model->SetLoadProgress(percentage * 0.5f);
		}
		return true;
	}
};

Model::Model()
{
}

void Model::LoadModelCPU(const std::string& fileName)
{
	this->filePath = fileName;
	minBound = glm::vec3(1e10);
	maxBound = glm::vec3(-1e10);

	Assimp::Importer importer;
	importer.SetProgressHandler(new ModelProgressHandler(this));
	
	const aiScene* scene = nullptr;
	std::string cachePath = fileName + ".assbin";

	// Try loading binary cache first (insanely fast)
	if (std::filesystem::exists(cachePath)) {
		printf("[Assimp] Loading from ultra-fast binary cache: %s\n", cachePath.c_str());
		scene = importer.ReadFile(cachePath, 0); // No post-processing needed, already baked!
		if (scene) SetLoadProgress(0.5f);
	}

	// Fallback to slow OBJ parsing + save binary cache
	if (!scene) {
		scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);
		
		if (scene) {
			Assimp::Exporter exporter;
			if (exporter.Export(scene, "assbin", cachePath) == aiReturn_SUCCESS) {
				printf("[Assimp] Generated binary cache: %s\n", cachePath.c_str());
			}
		}
	}

	if (!scene)
	{
		printf("Model [%s] failed to load: %s!\n", fileName.c_str(), importer.GetErrorString());
		loadFailed = true;
		return;
	}

	// start from first node
	LoadNode(scene->mRootNode, scene);

	// LoadMaterials now only prepares the paths, loading happens in GPU phase or deferred
	LoadMaterials(scene);

	isCPUReady = true;
}

void Model::LoadModelGPU()
{
	if (isGPUReady) return;
	if (loadFailed || !isCPUReady) {
		isGPUReady = true; // Still mark as ready so waiters aren't blocked, but with no meshes
		return;
	}

	for (auto& im : intermediateMeshes)
	{
		Mesh* newMesh = new Mesh();
		newMesh->CreateMesh(&im.vertices[0], &im.indices[0], (unsigned int)im.vertices.size(), (unsigned int)im.indices.size());
		
		// Set bounds from CPU data
		glm::vec3 min(1e10f), max(-1e10f);
		for (size_t v = 0; v < im.vertices.size() / 14; v++) {
			glm::vec3 p(im.vertices[v * 14], im.vertices[v * 14 + 1], im.vertices[v * 14 + 2]);
			min = glm::min(min, p);
			max = glm::max(max, p);
		}
		newMesh->SetBounds(min, max);
		newMesh->AddRef(); // Model claims ownership reference

		meshList.push_back(newMesh);
		meshNames.push_back(im.name);
		meshToTex.push_back(im.materialIndex);

		MeshData md;
		md.vertices = im.vertices;
		md.indices = im.indices;
		meshDataList.push_back(md);
	}

	// Load textures (GPU side)
	for (auto* tex : textureList) {
		if (tex) tex->LoadTextureGPU();
	}
	for (auto* tex : normalMapList) {
		if (tex) tex->LoadTextureGPU();
	}

	intermediateMeshes.clear();
	isGPUReady = true;
}

void Model::LoadNode(aiNode* node, const aiScene* scene)
{
	for (size_t i = 0; i < node->mNumMeshes; i++)
	{
		LoadMesh(scene->mMeshes[node->mMeshes[i]], scene);
	}

	for (size_t i = 0; i < node->mNumChildren; i++)
	{
		LoadNode(node->mChildren[i], scene);
	}
}

void Model::LoadMesh(aiMesh* mesh, const aiScene* scene)
{
	std::vector<GLfloat> vertices;
	std::vector<unsigned int> indices;

	// add vertices
	for (size_t i = 0; i < mesh->mNumVertices; i++)
	{
		// x y z positions
		vertices.insert(vertices.end(), { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z });
		
		// Update bounds
		minBound.x = std::min(minBound.x, mesh->mVertices[i].x);
		minBound.y = std::min(minBound.y, mesh->mVertices[i].y);
		minBound.z = std::min(minBound.z, mesh->mVertices[i].z);
		maxBound.x = std::max(maxBound.x, mesh->mVertices[i].x);
		maxBound.y = std::max(maxBound.y, mesh->mVertices[i].y);
		maxBound.z = std::max(maxBound.z, mesh->mVertices[i].z);

		// u v texture coordinates 
		if (mesh->mTextureCoords[0])
		{
			vertices.insert(vertices.end(), { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y });
		}
		else
		{
			vertices.insert(vertices.end(), { 0.0f, 0.0f });
		}

		// normals
		vertices.insert(vertices.end(), { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z });

		if (mesh->mTangents)
			vertices.insert(vertices.end(), { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z });
		else
			vertices.insert(vertices.end(), { 0.0f, 0.0f, 0.0f });

		if (mesh->mBitangents)
			vertices.insert(vertices.end(), { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z });
		else
			vertices.insert(vertices.end(), { 0.0f, 0.0f, 0.0f });
	}

	for (size_t i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (size_t j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	intermediateMeshes.push_back({ vertices, indices, mesh->mMaterialIndex, std::string(mesh->mName.C_Str()) });
}

void Model::LoadMaterials(const aiScene* scene)
{
	textureList.resize(scene->mNumMaterials);
	normalMapList.resize(scene->mNumMaterials);
	materialList.resize(scene->mNumMaterials);

	std::string directory = "";
	size_t slashPos = filePath.find_last_of("/\\");
	if (slashPos != std::string::npos) {
		directory = filePath.substr(0, slashPos + 1);
	}

	std::map<std::string, Texture*> textureCache;

	for (size_t i = 0; i < scene->mNumMaterials; i++)
	{
		aiMaterial* material = scene->mMaterials[i];

		textureList[i] = nullptr;
		normalMapList[i] = nullptr;

		if (material->GetTextureCount(aiTextureType_DIFFUSE))
		{
			aiString path;
			if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == aiReturn_SUCCESS)
			{
				std::string pathStr = std::string(path.data);
				// Strip OBJ flags like "-bm 0.001" which Assimp sometimes leaves in
				size_t lastSpace = pathStr.find_last_of(' ');
				if (lastSpace != std::string::npos && pathStr.find('-') != std::string::npos) {
					std::string lastPart = pathStr.substr(lastSpace + 1);
					if (lastPart.find('.') != std::string::npos) pathStr = lastPart;
				}

				size_t idx = pathStr.find_last_of("/\\");
				std::string filename = pathStr.substr(idx == std::string::npos ? 0 : idx + 1);
				
				std::string texPath = directory + filename;
				FILE* testFile = nullptr;
				if (fopen_s(&testFile, texPath.c_str(), "r") == 0) {
					fclose(testFile);
				} else {
					// Fallback to absolute/original path from Assimp
					texPath = directory + pathStr;
					if (fopen_s(&testFile, texPath.c_str(), "r") == 0) {
						fclose(testFile);
					} else {
						texPath = std::string("Assets/Textures/") + filename;
					}
				}

				if (textureCache.find(texPath) == textureCache.end()) {
					textureCache[texPath] = new Texture(texPath.c_str());
				}
				textureList[i] = textureCache[texPath];

				// Robust Normal Map Detection (Suffixes & Prefixes)
				size_t dotPos = texPath.rfind('.');
				if (dotPos != std::string::npos)
				{
					std::string base = texPath.substr(0, dotPos);
					std::string ext = texPath.substr(dotPos);
					std::string foundNormal = "";

					// 1. Try suffixes
					std::vector<std::string> suffixes = { "_normal", "_bump", "_n", "_norm", "_NM", "_BUMP", "_NORMAL" };
					for (const auto& s : suffixes) {
						std::string p = base + s + ext;
						FILE* f = nullptr;
						if (fopen_s(&f, p.c_str(), "r") == 0) { fclose(f); foundNormal = p; break; }
					}

					// 2. Try prefixes (e.g., N_leaf.png)
					if (foundNormal.empty()) {
						size_t lastSlash = base.find_last_of("/\\");
						std::string dir = (lastSlash == std::string::npos) ? "" : base.substr(0, lastSlash + 1);
						std::string name = (lastSlash == std::string::npos) ? base : base.substr(lastSlash + 1);
						std::vector<std::string> prefixes = { "N_", "n_", "Normal_", "norm_" };
						for (const auto& pr : prefixes) {
							std::string p = dir + pr + name + ext;
							FILE* f = nullptr;
							if (fopen_s(&f, p.c_str(), "r") == 0) { fclose(f); foundNormal = p; break; }
						}
					}

					if (!foundNormal.empty())
					{
						if (textureCache.find(foundNormal) == textureCache.end()) {
							textureCache[foundNormal] = new Texture(foundNormal.c_str());
						}
						normalMapList[i] = textureCache[foundNormal];
					}
				}
			}
		}
		if (!textureList[i])
		{
			std::string defPath = "Assets/Textures/plain.png";
			if (textureCache.find(defPath) == textureCache.end()) {
				textureCache[defPath] = new Texture(defPath.c_str());
			}
			textureList[i] = textureCache[defPath];
		}

		// Load Normal/Bump Map from Assimp
		if (!normalMapList[i])
		{
			// Check every possible type where a normal/bump map could be hiding in OBJ/MTL
			aiTextureType types[] = { aiTextureType_NORMALS, aiTextureType_HEIGHT, aiTextureType_DISPLACEMENT, aiTextureType_UNKNOWN };
			for (int t = 0; t < 4; t++)
			{
				if (material->GetTextureCount(types[t]))
				{
					aiString path;
					if (material->GetTexture(types[t], 0, &path) == aiReturn_SUCCESS)
					{
						std::string pathStr = std::string(path.data);
						// Strip OBJ flags like "-bm 0.001"
						size_t lastSpace = pathStr.find_last_of(' ');
						if (lastSpace != std::string::npos && pathStr.find('-') != std::string::npos) {
							std::string lastPart = pathStr.substr(lastSpace + 1);
							if (lastPart.find('.') != std::string::npos) pathStr = lastPart;
						}

						size_t idx = pathStr.find_last_of("/\\");
						std::string filename = pathStr.substr(idx == std::string::npos ? 0 : idx + 1);
						std::string nPath = directory + filename;
						FILE* testFile = nullptr;
						if (fopen_s(&testFile, nPath.c_str(), "r") != 0) {
							// Fallback: check if the path from Assimp was actually relative
							nPath = directory + pathStr;
							fopen_s(&testFile, nPath.c_str(), "r");
						}

						if (testFile) {
							fclose(testFile);
							if (textureCache.find(nPath) == textureCache.end()) {
								textureCache[nPath] = new Texture(nPath.c_str());
							}
							normalMapList[i] = textureCache[nPath];
							printf("[Assimp] Found Normal Map (%d) at: %s\n", (int)types[t], nPath.c_str());
							break;
						}
					}
				}
			}
		}

		Material* mat = new Material();
		
		aiColor3D diffuse(1.0f, 1.0f, 1.0f);
		if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == aiReturn_SUCCESS) {
			mat->SetColor(glm::vec3(diffuse.r, diffuse.g, diffuse.b));
		}
		
		aiColor3D ambient(1.0f, 1.0f, 1.0f);
		if (material->Get(AI_MATKEY_COLOR_AMBIENT, ambient) == aiReturn_SUCCESS) {
			mat->SetVec3("material.ambientColor", glm::vec3(ambient.r, ambient.g, ambient.b));
		}
		
		aiColor3D specular(1.0f, 1.0f, 1.0f);
		if (material->Get(AI_MATKEY_COLOR_SPECULAR, specular) == aiReturn_SUCCESS) {
			mat->SetVec3("material.specularColor", glm::vec3(specular.r, specular.g, specular.b));
		}
		
		float shininess = 32.0f;
		if (material->Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS) {
			mat->SetShininess(shininess);
		}
		
		float specIntensity = 0.5f;
		if (material->Get(AI_MATKEY_SHININESS_STRENGTH, specIntensity) == aiReturn_SUCCESS) {
			mat->SetSpecularIntensity(specIntensity);
		} else if (specular.r > 0 || specular.g > 0 || specular.b > 0) {
			mat->SetSpecularIntensity((specular.r + specular.g + specular.b) / 3.0f);
		}
		
		float opacity = 1.0f;
		if (material->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS) {
			mat->SetAlpha(opacity);
		}

		if (textureList[i]) {
			mat->SetTextureParam("material.diffuseMap", textureList[i]->GetFileLocation());
		}
		if (normalMapList[i]) {
			mat->SetTextureParam("material.normalMap", normalMapList[i]->GetFileLocation());
		}
		
		materialList[i] = mat;
	}

	// Multithreaded execution for Textures - throttled to prevent crashing on 1GB+ scenes
	int totalTextures = (int)textureCache.size();
	if (totalTextures > 0) {
		std::atomic<int> loadedTextures = 0;
		std::atomic<int> currentTexIndex = 0;
		std::vector<Texture*> uniqueTextures;
		for (auto& pair : textureCache) uniqueTextures.push_back(pair.second);

		// Use a fixed number of threads (hardware concurrency) to avoid thrashing
		unsigned int numThreads = std::thread::hardware_concurrency();
		if (numThreads == 0) numThreads = 4;
		std::vector<std::future<void>> workers;

		for (unsigned int t = 0; t < numThreads; t++) {
			workers.push_back(std::async(std::launch::async, [&]() {
				while (true) {
					int idx = currentTexIndex.fetch_add(1);
					if (idx >= totalTextures) break;

					uniqueTextures[idx]->LoadTextureCPU();
					loadedTextures++;
					SetLoadProgress(0.5f + 0.5f * ((float)loadedTextures / totalTextures));
				}
			}));
		}

		for (auto& w : workers) {
			w.wait();
		}
	}
	SetLoadProgress(1.0f);
}


void Model::ClearModel()
{
	for (size_t i = 0; i < meshList.size(); i++)
	{
		if (meshList[i])
		{
			meshList[i]->Release(); // Use reference counting instead of direct delete
			meshList[i] = nullptr;
		}
	}
	meshList.clear();
	meshNames.clear();

	std::vector<Texture*> toDelete;
	for (auto* tex : textureList) {
		if (tex && std::find(toDelete.begin(), toDelete.end(), tex) == toDelete.end()) {
			toDelete.push_back(tex);
		}
	}
	for (auto* tex : normalMapList) {
		if (tex && std::find(toDelete.begin(), toDelete.end(), tex) == toDelete.end()) {
			toDelete.push_back(tex);
		}
	}
	for (auto* tex : toDelete) {
		delete tex;
	}
	
	textureList.clear();
	normalMapList.clear();

	for (size_t i = 0; i < materialList.size(); i++)
	{
		if (materialList[i])
		{
			delete materialList[i];
			materialList[i] = nullptr;
		}
	}
}

void Model::RenderModel(GLuint uniformUseNormalMap, GLuint uniformUseDiffuseTexture, GLuint uniformNormalMapSampler, GLuint uniformDiffuseTextureSampler)
{
	if (!isGPUReady) return;

	for (size_t i = 0; i < meshList.size(); i++)
	{
		unsigned int materialIndex = meshToTex[i];

		if (materialIndex < textureList.size() && textureList[materialIndex])
		{
			glUniform1i(uniformUseDiffuseTexture, 1);
			glUniform1i(uniformDiffuseTextureSampler, 0); // Diffuse to Unit 0
			textureList[materialIndex]->UseTexture();
		}
		else
		{
			glUniform1i(uniformUseDiffuseTexture, 0);
		}

		if (materialIndex < normalMapList.size() && normalMapList[materialIndex])
		{
			glUniform1i(uniformUseNormalMap, 1);
			glUniform1i(uniformNormalMapSampler, 1); // Normal to Unit 1
			normalMapList[materialIndex]->UseNormalMap();
		}
		else
		{
			glUniform1i(uniformUseNormalMap, 0);
		}

		meshList[i]->RenderMesh();
	}
}

void Model::RenderModelGeometryOnly()
{
	if (!isGPUReady) return;

	for (size_t i = 0; i < meshList.size(); i++)
	{
		meshList[i]->RenderMesh();
	}
}


Model::~Model()
{
}


