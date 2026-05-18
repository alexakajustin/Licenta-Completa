#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "AssetBrowser.h"
#include "SceneManager.h"
#include "Material.h"
#include "PrimitiveGenerator.h"
#include "AssetManager.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>
#include <new>

#include "stb_image.h"

AssetBrowser::AssetBrowser()
	: currentAssetPath("Assets")
{
}

AssetBrowser::~AssetBrowser()
{
	CleanupThumbnailFBO();

	for (auto const& [key, val] : assetTextureCache) {
		if (val) {
			val->ClearTexture();
			delete val;
		}
	}
	if (folderIconSlot) { folderIconSlot->ClearTexture(); delete folderIconSlot; }
	if (modelIconSlot) { modelIconSlot->ClearTexture(); delete modelIconSlot; }
}

void AssetBrowser::Init()
{
	InitThumbnailFBO();
	RefreshAssetList();
}

void AssetBrowser::LoadAssetIcons()
{
	if (!folderIconSlot) {
		folderIconSlot = new Texture("Assets/Textures/plain.png");
		folderIconSlot->LoadTextureA();
	}
	if (!modelIconSlot) {
		modelIconSlot = new Texture("Assets/Textures/plain.png");
		modelIconSlot->LoadTextureA();
	}
}

void AssetBrowser::InitThumbnailFBO()
{
	if (thumbnailFBO != 0) CleanupThumbnailFBO();

	glGenFramebuffers(1, &thumbnailFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, thumbnailFBO);

	glGenTextures(1, &thumbnailTexture);
	glBindTexture(GL_TEXTURE_2D, thumbnailTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, thumbnailSize, thumbnailSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, thumbnailTexture, 0);

	glGenRenderbuffers(1, &thumbnailDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, thumbnailDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, thumbnailSize, thumbnailSize);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, thumbnailDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("Thumbnail Framebuffer is not complete!\n");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	thumbnailShader.CreateFromFiles("Shaders/thumbnail.vert", "Shaders/thumbnail.frag");
}

void AssetBrowser::CleanupThumbnailFBO()
{
	if (thumbnailFBO != 0) { glDeleteFramebuffers(1, &thumbnailFBO); thumbnailFBO = 0; }
	if (thumbnailTexture != 0) { glDeleteTextures(1, &thumbnailTexture); thumbnailTexture = 0; }
	if (thumbnailDepth != 0) { glDeleteRenderbuffers(1, &thumbnailDepth); thumbnailDepth = 0; }
}

bool AssetBrowser::GenerateModelThumbnail(const std::filesystem::path& modelPath, Texture* targetSlot)
{
	if (thumbnailFBO == 0) return false;

	// === GLOBAL MEMORY SAFETY ===
	// Skip thumbnail generation to avoid crash if RAM is critically low
	PROCESS_MEMORY_COUNTERS_EX pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
	{
		size_t usedBytes = pmc.PrivateUsage;
#ifdef _WIN64
		const size_t CRITICAL_THRESHOLD = (size_t)(16000ULL * 1024ULL * 1024ULL); // 16 GB limit for 64-bit
#else
		const size_t CRITICAL_THRESHOLD = (size_t)(1800 * 1024 * 1024); // 1.8GB limit for 32-bit
#endif
		if (usedBytes > CRITICAL_THRESHOLD) {
			printf("[AssetBrowser] RAM critical (%.0f MB used). Skipping thumbnail for safety: %s\n", usedBytes / (1024.0f * 1024.0f), modelPath.string().c_str());
			thumbnailGenerationMap[modelPath.string()] = true; // Mark as "done/skipped" so we don't spam the console/RAM check
			return true; 
		}
	}

	Model* tempModel = AssetManager::Get().GetModel(modelPath.string());
	
	if (!tempModel->IsReady()) {
		if (tempModel->IsFailed()) {
			printf("[AssetBrowser] Model failed to load for thumbnail: %s\n", modelPath.string().c_str());
			// Set a placeholder or just return true to stop trying
			return true; 
		}
		return false; // Not ready yet
	}

	// Flush pending GL errors
	while (glGetError() != GL_NO_ERROR);

	// Save full GL state
	GLint oldViewport[4];
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	GLint oldFBO;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	GLint oldProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
	GLboolean oldCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean oldDepthTest = glIsEnabled(GL_DEPTH_TEST);

	glViewport(0, 0, thumbnailSize, thumbnailSize);
	glBindFramebuffer(GL_FRAMEBUFFER, thumbnailFBO);
	glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);
	
	GLboolean oldBlend = glIsEnabled(GL_BLEND);
	glDisable(GL_BLEND);

	thumbnailShader.UseShader();
	
	// Auto-frame with robust camera
	glm::vec3 minB = tempModel->GetMinBound();
	glm::vec3 maxB = tempModel->GetMaxBound();
	glm::vec3 center = (minB + maxB) * 0.5f;
	glm::vec3 size = maxB - minB;
	float maxDim = std::max({ size.x, size.y, size.z });
	if (maxDim < 0.001f) maxDim = 1.0f;

	float cameraDist = maxDim * 2.0f;
	glm::mat4 projection = glm::perspective(glm::radians(35.0f), 1.0f, cameraDist * 0.01f, cameraDist * 10.0f);
	glm::vec3 camOffset = glm::normalize(glm::vec3(1.0f, 0.8f, 1.0f)) * cameraDist;
	glm::mat4 view = glm::lookAt(center + camOffset, center, glm::vec3(0, 1, 0));
	
	glUniformMatrix4fv(thumbnailShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(thumbnailShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	
	glm::mat4 model = glm::mat4(1.0f);
	glUniformMatrix4fv(thumbnailShader.GetModelLocation(), 1, GL_FALSE, glm::value_ptr(model));

	glUniform1i(glGetUniformLocation(thumbnailShader.GetShaderID(), "theTexture"), 0);
	GLuint useDiffuseLoc = glGetUniformLocation(thumbnailShader.GetShaderID(), "useDiffuseTexture");
	GLuint useNormalLoc = glGetUniformLocation(thumbnailShader.GetShaderID(), "useNormalMap"); // Fallback if shader doesn't have it

	// Disable material mode for model thumbnails (models use their own textures)
	glUniform1i(glGetUniformLocation(thumbnailShader.GetShaderID(), "useMaterial"), 0);
	glUniform3f(glGetUniformLocation(thumbnailShader.GetShaderID(), "materialColor"), 1.0f, 1.0f, 1.0f);

	// Use RenderModel properly by passing the correct uniform locations
	tempModel->RenderModel(useNormalLoc, useDiffuseLoc, 
		glGetUniformLocation(thumbnailShader.GetShaderID(), "normalMap"),
		glGetUniformLocation(thumbnailShader.GetShaderID(), "theTexture")); 

	// Read back
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	unsigned char* data = new(std::nothrow) unsigned char[thumbnailSize * thumbnailSize * 4];
	if (!data) {
		printf("[AssetBrowser] Critical: Memory exhausted, cannot generate thumbnail.\n");
		// Restore GL state and bail
		glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
		glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
		glUseProgram(oldProgram);
		if (oldCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
		if (oldDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		if (oldBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
		return true; // Return true to stop trying to generate this one
	}
	
	glReadPixels(0, 0, thumbnailSize, thumbnailSize, GL_RGBA, GL_UNSIGNED_BYTE, data);

	GLuint newTexID;
	glGenTextures(1, &newTexID);
	glBindTexture(GL_TEXTURE_2D, newTexID);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, thumbnailSize, thumbnailSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	
	targetSlot->SetTextureID(newTexID);

	delete[] data;

	// Restore full GL state
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	glUseProgram(oldProgram);
	if (oldCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (oldDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (oldBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);

	thumbnailGenerationMap[modelPath.string()] = true; // Mark as SUCCESS
	return true;
}

void AssetBrowser::GenerateMaterialThumbnail(const std::string& matPath, Texture* targetSlot)
{
	if (thumbnailFBO == 0) return;

	// Load material properties
	Material* mat = Material::LoadFromFile(matPath);
	if (!mat) return;

	// Create sphere mesh on first call
	static Mesh* sphereMesh = nullptr;
	if (!sphereMesh) sphereMesh = PrimitiveGenerator::CreateSphere(24, 24);

	// Flush pending GL errors
	while (glGetError() != GL_NO_ERROR);

	// Save full GL state
	GLint oldViewport[4], oldFBO, oldProgram;
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
	GLboolean oldCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean oldDepthTest = glIsEnabled(GL_DEPTH_TEST);

	glViewport(0, 0, thumbnailSize, thumbnailSize);
	glBindFramebuffer(GL_FRAMEBUFFER, thumbnailFBO);
	glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	thumbnailShader.UseShader();

	// Camera for sphere (fits nicely in view)
	glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
	glm::mat4 model = glm::mat4(1.0f);

	glUniformMatrix4fv(thumbnailShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(thumbnailShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(thumbnailShader.GetModelLocation(), 1, GL_FALSE, glm::value_ptr(model));
	glUniform1i(glGetUniformLocation(thumbnailShader.GetShaderID(), "theTexture"), 0);
	glUniform1i(glGetUniformLocation(thumbnailShader.GetShaderID(), "useDiffuseTexture"), 0);

	// Pass material properties so each material thumbnail looks different
	GLuint shaderID = thumbnailShader.GetShaderID();
	glm::vec3 matColor = mat->GetColor();
	glUniform3f(glGetUniformLocation(shaderID, "materialColor"), matColor.r, matColor.g, matColor.b);
	glUniform1f(glGetUniformLocation(shaderID, "specularIntensity"), mat->GetSpecularIntensity());
	glUniform1f(glGetUniformLocation(shaderID, "shininess"), mat->GetShininess());
	glUniform1i(glGetUniformLocation(shaderID, "useMaterial"), 1);

	sphereMesh->RenderMesh();

	// Read back
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	unsigned char* data = new unsigned char[thumbnailSize * thumbnailSize * 4];
	glReadPixels(0, 0, thumbnailSize, thumbnailSize, GL_RGBA, GL_UNSIGNED_BYTE, data);

	GLuint newTexID;
	glGenTextures(1, &newTexID);
	glBindTexture(GL_TEXTURE_2D, newTexID);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, thumbnailSize, thumbnailSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);

	targetSlot->SetTextureID(newTexID);

	delete[] data;
	delete mat;

	// Restore GL state
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	glUseProgram(oldProgram);
	if (oldCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (oldDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void AssetBrowser::RefreshAssetList()
{
	currentAssets.clear();
	LoadAssetIcons();

	if (!std::filesystem::exists(currentAssetPath)) {
		currentAssetPath = "Assets";
		if (!std::filesystem::exists(currentAssetPath)) return;
	}

	for (auto const& entry : std::filesystem::directory_iterator(currentAssetPath))
	{
		AssetInfo info;
		info.name = entry.path().filename().string();
		info.path = entry.path();
		
		if (entry.is_directory()) {
			info.type = AssetType::Folder;
			info.thumbnail = folderIconSlot;
		}
		else {
			std::string ext = entry.path().extension().string();
			for (auto& c : ext) c = tolower(c);

			if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
				info.type = AssetType::Texture;
				std::string pStr = entry.path().string();
				if (assetTextureCache.count(pStr)) {
					info.thumbnail = assetTextureCache[pStr];
				}
				else {
					Texture* tex = new Texture("Assets/Textures/plain.png");
					tex->LoadTextureA();
					assetTextureCache[pStr] = tex;
					info.thumbnail = tex;

					asyncTextureTasks.push_back(std::async(std::launch::async, [pStr]() {
						TextureLoadData* data = new TextureLoadData();
						data->path = pStr;
						data->data = stbi_load(pStr.c_str(), &data->width, &data->height, &data->bitDepth, 4);
						return data;
					}));
				}
			}
			else if (ext == ".obj" || ext == ".fbx" || ext == ".dae" || ext == ".gltf") {
				info.type = AssetType::Model;
				std::string pStr = entry.path().string();
				if (assetTextureCache.count(pStr)) {
					info.thumbnail = assetTextureCache[pStr];
				}
				else {
					// Use a placeholder first, we will generate the thumbnail once ready in Render()
					Texture* tex = new Texture("Assets/Textures/plain.png");
					tex->LoadTextureA();
					assetTextureCache[pStr] = tex;
					info.thumbnail = tex;
				}
			}
			else if (ext == ".mat") {
				info.type = AssetType::MaterialAsset;
				std::string pStr = entry.path().string();
				if (assetTextureCache.count(pStr)) {
					info.thumbnail = assetTextureCache[pStr];
				}
				else {
					// Generate sphere thumbnail for material
					Texture* tex = new Texture();
					GenerateMaterialThumbnail(pStr, tex);
					assetTextureCache[pStr] = tex;
					info.thumbnail = tex;
				}
			}
			else {
				continue;
			}
		}
		currentAssets.push_back(info);
	}
}

void AssetBrowser::Render(SceneManager& scene, EditorUI::WindowState& uiState)
{
	if (!uiState.isAssetBrowserOpen) return;

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winWidth = displaySize.x;
	float winHeight = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();
	
	// Project on bottom-middle
	ImVec2 pos(uiState.leftWidth, menuHeight + (winHeight - menuHeight) * uiState.midHeightRatio);
	ImVec2 size(winWidth - uiState.leftWidth - uiState.rightWidth, (winHeight - menuHeight) * (1.0f - uiState.midHeightRatio));

	if (uiState.maximizedWindowID == 3) { // Project Maximized
		pos = ImVec2(0, menuHeight);
		size = ImVec2(winWidth, winHeight - menuHeight);
	} else if (uiState.maximizedWindowID != -1) { // Something ELSE maximized
		return;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
	
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	bool projectOpen = ImGui::Begin("Project", &uiState.isAssetBrowserOpen, windowFlags);
	ImGui::PopStyleVar();

	if (projectOpen)
	{
		uiState.CheckMaximize(3);

		// Choose which asset list to display early so we process deferred thumbnails for it
		std::vector<AssetInfo>& displayAssets = isSearching ? searchResults : currentAssets;

		// Update deferred thumbnails
		for (auto& asset : displayAssets) {
			if (asset.type == AssetType::Model) {
				std::string pStr = asset.path.string();
				
				// ONLY try to generate if we haven't successfully done so before
				if (thumbnailGenerationMap.find(pStr) == thumbnailGenerationMap.end()) {
					bool canLoad = AssetManager::Get().GetActiveTasksCount() < 2;
					Model* model = AssetManager::Get().GetModel(pStr, canLoad);
					if (model) {
						if (model->IsReady() && !model->IsFailed()) {
							GenerateModelThumbnail(asset.path, asset.thumbnail);
						}
						else if (model->IsFailed()) {
							thumbnailGenerationMap[pStr] = true; // Just mark as "done" so we don't keep checking failed models
						}
					}
				}
			}
		}

		// Process async textures
		for (auto it = asyncTextureTasks.begin(); it != asyncTextureTasks.end(); ) {
			if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
				TextureLoadData* res = it->get();
				if (res->data) {
					Texture* tex = new Texture(res->path.c_str());
					tex->LoadTextureFromData(res->data, res->width, res->height, 4);
					
					// Replace placeholder
					if (assetTextureCache.count(res->path)) {
						Texture* old = assetTextureCache[res->path];
						if (old) {
							old->ClearTexture();
							delete old;
						}
					}
					assetTextureCache[res->path] = tex;
					
					// Update currentAssets and searchResults pointing to it
					for (auto& a : currentAssets) {
						if (a.path.string() == res->path) {
							a.thumbnail = tex;
							break;
						}
					}
					for (auto& a : searchResults) {
						if (a.path.string() == res->path) {
							a.thumbnail = tex;
							break;
						}
					}
				}
				if (res->data) stbi_image_free(res->data);
				delete res;
				it = asyncTextureTasks.erase(it);
			} else {
				++it;
			}
		}

		if (ImGui::Button("Refresh")) {
			RefreshAssetList();
			searchResults.clear();
			if (strlen(searchBuffer) > 0) isSearching = true; // Re-search after refresh
		}
		ImGui::SameLine();
		if (ImGui::Button("..")) {
			if (currentAssetPath.has_parent_path() && currentAssetPath != "Assets") {
				currentAssetPath = currentAssetPath.parent_path();
				RefreshAssetList();
				// Clear search when navigating
				searchBuffer[0] = '\0';
				isSearching = false;
				searchResults.clear();
			}
		}
		ImGui::SameLine();

		// ===== Unity-style Search Bar =====
		ImGui::PushItemWidth(200.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		
		bool searchChanged = ImGui::InputTextWithHint("##AssetSearch", "Search assets...", searchBuffer, sizeof(searchBuffer));
		
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		ImGui::PopItemWidth();

		// Clear button (X) when there's text
		if (strlen(searchBuffer) > 0) {
			ImGui::SameLine();
			if (ImGui::SmallButton("X")) {
				searchBuffer[0] = '\0';
				isSearching = false;
				searchResults.clear();
				searchChanged = true;
			}
		}

		static float searchDebounceTimer = 0.0f;
		static bool searchDebouncePending = false;

		if (searchChanged) {
			searchDebounceTimer = 0.0f;
			searchDebouncePending = true;
			
			// If the user completely cleared the text, process it instantly
			if (strlen(searchBuffer) == 0) {
				isSearching = false;
				searchPending = false;
				searchResults.clear();
				currentSearchGeneration++; // abort active searches
				searchDebouncePending = false;
			}
		}

		if (searchDebouncePending) {
			searchDebounceTimer += ImGui::GetIO().DeltaTime;
			if (searchDebounceTimer >= 0.4f) { // 400ms debounce
				searchDebouncePending = false;

				std::string query(searchBuffer);
				if (query.length() > 0) {
					isSearching = true;
					searchPending = true;
					lastSearchQuery = query;

					// Increment generation to discard any old running threads
					int generation = ++currentSearchGeneration;

					// Launch filesystem scan on detached background thread (NO blocking on destruction!)
					std::filesystem::path searchRoot = currentAssetPath;
					std::thread([this, searchRoot, query, generation]() {
						std::vector<AssetInfo> results;
						std::string lowerQuery = query;
						std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), [](unsigned char c){ return std::tolower(c); });

						try {
							for (const auto& entry : std::filesystem::recursive_directory_iterator(searchRoot)) {
								// If a new search was started, abort this one early
								if (this->currentSearchGeneration != generation) return;

								if (entry.is_directory()) continue;

								std::string filename = entry.path().filename().string();
								std::string lowerName = filename;
								std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c){ return std::tolower(c); });

								if (lowerName.find(lowerQuery) == std::string::npos) continue;

								std::string ext = entry.path().extension().string();
								for (auto& c : ext) c = std::tolower(c);

								AssetInfo info;
								info.name = filename;
								info.path = entry.path();
								info.thumbnail = nullptr; // Resolved on main thread

								if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga")
									info.type = AssetType::Texture;
								else if (ext == ".obj" || ext == ".fbx" || ext == ".dae" || ext == ".gltf")
									info.type = AssetType::Model;
								else if (ext == ".mat")
									info.type = AssetType::MaterialAsset;
								else
									continue;

								results.push_back(info);
							}
						} catch (...) {}

						// If this is still the active search, post results
						if (this->currentSearchGeneration == generation) {
							std::lock_guard<std::mutex> lock(this->searchMutex);
							this->asyncSearchResults = results;
							this->searchPending = false;
						}
					}).detach();
				}
			}
		}

		// Poll for async search results (non-blocking)
		if (!searchPending && isSearching) {
			bool newResults = false;
			{
				std::lock_guard<std::mutex> lock(searchMutex);
				if (!asyncSearchResults.empty() || strlen(searchBuffer) > 0) {
					// We only swap if we had a successful completion for the current text
					if (searchResults.size() != asyncSearchResults.size() || 
						(!searchResults.empty() && !asyncSearchResults.empty() && searchResults[0].path != asyncSearchResults[0].path)) {
						searchResults = asyncSearchResults;
						newResults = true;
					}
				}
			}

			// Resolve thumbnails on main thread using cache (only when new results arrive)
			if (newResults) {
				for (auto& info : searchResults) {
					std::string pStr = info.path.string();
					if (assetTextureCache.count(pStr)) {
						info.thumbnail = assetTextureCache[pStr];
					} else if (info.type == AssetType::Texture) {
						Texture* tex = new Texture("Assets/Textures/plain.png");
						tex->LoadTextureA();
						assetTextureCache[pStr] = tex;
						info.thumbnail = tex;
						asyncTextureTasks.push_back(std::async(std::launch::async, [pStr]() {
							TextureLoadData* data = new TextureLoadData();
							data->path = pStr;
							data->data = stbi_load(pStr.c_str(), &data->width, &data->height, &data->bitDepth, 4);
							return data;
						}));
					} else if (info.type == AssetType::Model) {
						Texture* tex = new Texture("Assets/Textures/plain.png");
						tex->LoadTextureA();
						assetTextureCache[pStr] = tex;
						info.thumbnail = tex;
					} else if (info.type == AssetType::MaterialAsset) {
						Texture* tex = new Texture();
						GenerateMaterialThumbnail(pStr, tex);
						assetTextureCache[pStr] = tex;
						info.thumbnail = tex;
					}
				}
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled("Path: %s", currentAssetPath.string().c_str());

		// Show result count / searching indicator
		if (isSearching) {
			ImGui::SameLine();
			if (searchPending) {
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Searching...");
			} else {
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "(%d results)", (int)searchResults.size());
			}
		}

		ImGui::Separator();

		// displayAssets is already chosen at the top of the function


		float cellSize = 100.0f;
		float padding = 16.0f;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int columnCount = (int)(panelWidth / (cellSize + padding));
		if (columnCount < 1) columnCount = 1;

		ImGui::Columns(columnCount, 0, false);

		for (int i = 0; i < (int)displayAssets.size(); i++)
		{
			ImGui::PushID(i);
			
			ImVec4 tint = ImVec4(1, 1, 1, 1);
			if (displayAssets[i].type == AssetType::Folder) tint = ImVec4(1, 0.8f, 0.4f, 1);

			ImVec2 startPos = ImGui::GetCursorPos();
			bool isSelected = (displayAssets[i].path == selectedAssetPath);

			if (ImGui::Selectable("##selectable", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellSize, cellSize + 40))) {
				selectedAssetPath = displayAssets[i].path;
				
				if (ImGui::IsMouseDoubleClicked(0)) {
					if (displayAssets[i].type == AssetType::Folder) {
						currentAssetPath = displayAssets[i].path;
						RefreshAssetList();
						// Clear search on folder navigation
						searchBuffer[0] = '\0';
						isSearching = false;
						searchResults.clear();
						ImGui::PopID();
						break; 
					} else if (isSearching) {
						// If searching, double clicking a file jumps to its parent directory
						currentAssetPath = displayAssets[i].path.parent_path();
						RefreshAssetList();
						searchBuffer[0] = '\0';
						isSearching = false;
						searchResults.clear();
						ImGui::PopID();
						break;
					}
				}
			}

			// Drag-drop source
			if (ImGui::BeginDragDropSource()) {
				std::string pathStr = displayAssets[i].path.string();
				
				// Use different payload type for materials vs models
				const char* payloadType = (displayAssets[i].type == AssetType::MaterialAsset) ? "MATERIAL_PATH" : "ASSET_PATH";
				ImGui::SetDragDropPayload(payloadType, pathStr.c_str(), pathStr.size() + 1);
				
				ImGui::Text("Dragging %s", displayAssets[i].name.c_str());
				if (displayAssets[i].thumbnail) {
					ImGui::Image((ImTextureID)(intptr_t)displayAssets[i].thumbnail->GetTextureID(), ImVec2(32, 32), ImVec2(0, 1), ImVec2(1, 0));
				}
				ImGui::EndDragDropSource();
			}

			// Icon / Thumbnail
			ImGui::SetCursorPos(ImVec2(startPos.x + 5, startPos.y + 5));
			if (displayAssets[i].thumbnail) {
				ImTextureID texID = (ImTextureID)(intptr_t)displayAssets[i].thumbnail->GetTextureID();
				
				// Use ImageWithBg to support the 'tint' parameter for folders
				ImGui::ImageWithBg(texID, ImVec2(cellSize - 10, cellSize - 10), ImVec2(0, 1), ImVec2(1, 0), ImVec4(0,0,0,0), tint);
				
				// Show "Loading..." overlay for models that haven't generated their thumbnail yet
				if (displayAssets[i].type == AssetType::Model) {
					std::string pStr = displayAssets[i].path.string();
					if (thumbnailGenerationMap.find(pStr) == thumbnailGenerationMap.end()) {
						Model* model = AssetManager::Get().GetModel(pStr, false);
						if (model && !model->IsReady() && !model->IsFailed()) {
							ImGui::SetCursorPos(ImVec2(startPos.x + 5, startPos.y + cellSize - 20));
							char progStr[32];
							float prog = model->GetLoadProgress();
							snprintf(progStr, sizeof(progStr), "%d%%", (int)(prog * 100));
							
							ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
							ImGui::ProgressBar(prog, ImVec2(cellSize - 10, 15), progStr);
							ImGui::PopStyleColor();
						} else {
							ImGui::SetCursorPos(ImVec2(startPos.x + 10, startPos.y + cellSize / 2 - 5));
							ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Queued...");
						}
					} else {
						// It was attempted. Was it a failure?
						Model* model = AssetManager::Get().GetModel(pStr, false);
						if (model && model->IsFailed()) {
							ImGui::SetCursorPos(ImVec2(startPos.x + 5, startPos.y + cellSize - 20));
							ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Failed");
						} else {
							// If RAM is critical, it was skipped
							PROCESS_MEMORY_COUNTERS_EX pmc;
							if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
#ifdef _WIN64
								if (pmc.PrivateUsage > (size_t)(15500ULL * 1024ULL * 1024ULL)) {
#else
								if (pmc.PrivateUsage > (size_t)(1700 * 1024 * 1024)) {
#endif
									ImGui::SetCursorPos(ImVec2(startPos.x + 5, startPos.y + cellSize - 20));
									ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "RAM Limit");
								}
							}
						}
					}
				}
			}
			else {
				ImGui::Button("??", ImVec2(cellSize, cellSize));
			}

			ImGui::SetCursorPos(ImVec2(startPos.x, startPos.y + cellSize + 5));
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cellSize);
			ImGui::Text("%s", displayAssets[i].name.c_str());
			ImGui::PopTextWrapPos();

			ImGui::NextColumn();
			ImGui::PopID();
		}

		ImGui::Columns(1);
	}
	ImGui::End();
}
