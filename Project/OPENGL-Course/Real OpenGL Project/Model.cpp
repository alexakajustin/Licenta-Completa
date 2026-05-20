#include "Model.h"
#include <algorithm>

#include <assimp/Exporter.hpp>
#include <assimp/ProgressHandler.hpp>
#include <filesystem>
#include <future>
#include <mutex>

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
	for (size_t ti = 0; ti < textureList.size(); ti++) {
		if (textureList[ti]) {
			textureList[ti]->LoadTextureGPU();
		}
	}
	// Fallback: replace any texture that failed to load with plain.png
	static Texture* plainFallback = nullptr;
	for (size_t ti = 0; ti < textureList.size(); ti++) {
		if (!textureList[ti] || textureList[ti]->GetTextureID() == 0) {
			if (!plainFallback) {
				plainFallback = new Texture("Assets/Textures/plain.png");
				plainFallback->LoadTexture(); // Sync load on main thread (GL context available)
			}
			textureList[ti] = plainFallback;
			// Keep the original material color from Assimp — plain.png is white,
			// so shader computes: white_texture × material_color = material_color
			printf("[Model GPU] Texture[%d] replaced with plain.png fallback (GPU ID: %u)\n", (int)ti, plainFallback->GetTextureID());
		}
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

static std::map<std::string, std::string> s_globalTexturePathCache;
static std::map<std::string, std::string> s_globalTextureBaseNameCache;
static std::mutex s_globalTexturePathCacheMutex;
static bool s_globalTexturePathCacheBuilt = false;

static std::string FindTextureRecursively(const std::string& filename)
{
	if (filename.empty()) return "";
	
	// Convert filename to lowercase for case-insensitive matching
	std::string lowerFilename = filename;
	std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), [](unsigned char c){ return std::tolower(c); });
	
	// Extract base name
	std::string lowerBaseName = lowerFilename;
	size_t dotPos = lowerBaseName.find_last_of('.');
	if (dotPos != std::string::npos) {
		lowerBaseName = lowerBaseName.substr(0, dotPos);
	}

	std::lock_guard<std::mutex> lock(s_globalTexturePathCacheMutex);

	auto checkCaches = [&]() -> std::string {
		if (s_globalTexturePathCache.find(lowerFilename) != s_globalTexturePathCache.end()) {
			return s_globalTexturePathCache[lowerFilename];
		}
		if (s_globalTextureBaseNameCache.find(lowerBaseName) != s_globalTextureBaseNameCache.end()) {
			return s_globalTextureBaseNameCache[lowerBaseName];
		}
		return "";
	};

	std::string found = checkCaches();
	if (!found.empty()) return found;

	// If we already swept the whole Assets/ folder once, and it's still not found, it literally doesn't exist
	if (s_globalTexturePathCacheBuilt) {
		return "";
	}

	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator("Assets")) {
			if (entry.is_regular_file()) {
				std::string entryFilename = entry.path().filename().string();
				std::transform(entryFilename.begin(), entryFilename.end(), entryFilename.begin(), [](unsigned char c){ return std::tolower(c); });
				
				std::string entryBaseName = entryFilename;
				size_t eDotPos = entryBaseName.find_last_of('.');
				if (eDotPos != std::string::npos) {
					entryBaseName = entryBaseName.substr(0, eDotPos);
				}

				std::string pathStr = entry.path().string();
				std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
				
				// Cache full filename (lowercase)
				if (s_globalTexturePathCache.find(entryFilename) == s_globalTexturePathCache.end()) {
					s_globalTexturePathCache[entryFilename] = pathStr;
				}
				// Cache base name (lowercase)
				if (s_globalTextureBaseNameCache.find(entryBaseName) == s_globalTextureBaseNameCache.end()) {
					s_globalTextureBaseNameCache[entryBaseName] = pathStr;
				}
			}
		}
		s_globalTexturePathCacheBuilt = true;
	} catch (...) { }

	return checkCaches();
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

	std::string modelBaseName = "";
	if (slashPos != std::string::npos) modelBaseName = filePath.substr(slashPos + 1);
	else modelBaseName = filePath;
	size_t mdotPos = modelBaseName.find_last_of('.');
	if (mdotPos != std::string::npos) modelBaseName = modelBaseName.substr(0, mdotPos);

	std::map<std::string, Texture*> textureCache;

	auto tryLoadTexture = [&](const std::string& filename) -> std::string {
		std::string texPath = directory + filename;
		FILE* testFile = nullptr;
		if (fopen_s(&testFile, texPath.c_str(), "r") == 0) {
			fclose(testFile);
			return texPath;
		}
		std::string foundTex = FindTextureRecursively(filename);
		if (!foundTex.empty()) return foundTex;
		return "";
	};

	// Lambda: try to load an embedded texture from GLB/GLTF
	auto tryLoadEmbedded = [&](const aiScene* sc, const char* texPath) -> Texture* {
		const aiTexture* embTex = sc->GetEmbeddedTexture(texPath);
		if (!embTex) return nullptr;

		std::string cacheKey = std::string("*emb*") + texPath;
		if (textureCache.count(cacheKey)) return textureCache[cacheKey];

		Texture* tex = new Texture("(embedded)");
		if (embTex->mHeight == 0) {
			// Compressed format (PNG, JPEG, etc) — mWidth is byte count
			tex->LoadFromMemory((const unsigned char*)embTex->pcData, embTex->mWidth);
		} else {
			// Raw ARGB8888 pixels — convert to RGBA for stbi compatibility
			int pixelCount = embTex->mWidth * embTex->mHeight;
			unsigned char* rgba = (unsigned char*)malloc(pixelCount * 4);
			for (int p = 0; p < pixelCount; p++) {
				rgba[p*4+0] = embTex->pcData[p].r;
				rgba[p*4+1] = embTex->pcData[p].g;
				rgba[p*4+2] = embTex->pcData[p].b;
				rgba[p*4+3] = embTex->pcData[p].a;
			}
			tex->LoadTextureFromData(rgba, embTex->mWidth, embTex->mHeight, 4);
			free(rgba);
		}
		textureCache[cacheKey] = tex;
		return tex;
	};

	for (size_t i = 0; i < scene->mNumMaterials; i++)
	{
		aiMaterial* material = scene->mMaterials[i];

		textureList[i] = nullptr;
		normalMapList[i] = nullptr;

		std::string diffusePath = "";
		aiTextureType diffuseTypes[] = { aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR, aiTextureType_UNKNOWN };
		for (int t = 0; t < 3 && diffusePath.empty() && !textureList[i]; t++) {
			if (material->GetTextureCount(diffuseTypes[t])) {
				aiString path;
				if (material->GetTexture(diffuseTypes[t], 0, &path) == aiReturn_SUCCESS) {
					// Try embedded texture first (GLB/GLTF)
					Texture* embTex = tryLoadEmbedded(scene, path.C_Str());
					if (embTex) {
						textureList[i] = embTex;
						break;
					}

					std::string pathStr = std::string(path.data);
					size_t lastSpace = pathStr.find_last_of(' ');
					if (lastSpace != std::string::npos && pathStr.find('-') != std::string::npos) {
						std::string lastPart = pathStr.substr(lastSpace + 1);
						if (lastPart.find('.') != std::string::npos) pathStr = lastPart;
					}
					size_t idx = pathStr.find_last_of("/\\");
					std::string filename = pathStr.substr(idx == std::string::npos ? 0 : idx + 1);
					
					diffusePath = tryLoadTexture(filename);
					if (diffusePath.empty()) diffusePath = tryLoadTexture(pathStr);
				}
			}
		}

		if (diffusePath.empty() && !textureList[i]) {
			aiString aiMatName;
			material->Get(AI_MATKEY_NAME, aiMatName);
			std::string matName = aiMatName.C_Str();
			
			std::vector<std::string> guesses = {
				matName + "_BaseColor.png", matName + "_Diffuse.png", matName + "_Albedo.png",
				modelBaseName + "_BaseColor.png", modelBaseName + "_Diffuse.png", modelBaseName + "_Albedo.png",
				matName + ".png", matName + ".jpg", modelBaseName + ".png"
			};
			for (const auto& guess : guesses) {
				diffusePath = tryLoadTexture(guess);
				if (!diffusePath.empty()) {
					printf("[Assimp] Guessed Diffuse Texture: %s\n", diffusePath.c_str());
					break;
				}
			}
		}

		if (!diffusePath.empty() && !textureList[i])
		{
			if (textureCache.find(diffusePath) == textureCache.end()) {
				textureCache[diffusePath] = new Texture(diffusePath.c_str());
			}
			textureList[i] = textureCache[diffusePath];

			// Robust Normal Map Detection
			size_t dotPos = diffusePath.rfind('.');
			if (dotPos != std::string::npos)
			{
				std::string base = diffusePath.substr(0, dotPos);
				std::string ext = diffusePath.substr(dotPos);
				std::string foundNormal = "";

				std::vector<std::string> suffixes = { "_normal", "_bump", "_n", "_norm", "_NM", "_BUMP", "_NORMAL", "_Normal" };
				for (const auto& s : suffixes) {
					std::string p = base + s + ext;
					FILE* f = nullptr;
					if (fopen_s(&f, p.c_str(), "r") == 0) { fclose(f); foundNormal = p; break; }
				}

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
				
				// Try replacing BaseColor with Normal
				if (foundNormal.empty()) {
					std::string replaced = base;
					size_t bcPos = replaced.find("_BaseColor");
					if (bcPos != std::string::npos) {
						replaced.replace(bcPos, 10, "_Normal");
						FILE* f = nullptr;
						if (fopen_s(&f, (replaced + ext).c_str(), "r") == 0) { fclose(f); foundNormal = replaced + ext; }
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
						// Try embedded texture first (GLB/GLTF)
						Texture* embNorm = tryLoadEmbedded(scene, path.C_Str());
						if (embNorm) {
							normalMapList[i] = embNorm;
							break;
						}

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
							if (fopen_s(&testFile, nPath.c_str(), "r") != 0) {
								std::string foundNormal = FindTextureRecursively(filename);
								if (!foundNormal.empty()) {
									nPath = foundNormal;
									// dummy file open so logic below works
									fopen_s(&testFile, nPath.c_str(), "r");
								}
							}
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


