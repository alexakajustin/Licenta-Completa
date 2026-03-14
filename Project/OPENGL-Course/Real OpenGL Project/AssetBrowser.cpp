#include "AssetBrowser.h"
#include "SceneManager.h"
#include "Material.h"
#include "PrimitiveGenerator.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

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

void AssetBrowser::GenerateModelThumbnail(const std::filesystem::path& modelPath, Texture* targetSlot)
{
	if (thumbnailFBO == 0) return;

	Model tempModel;
	tempModel.LoadModel(modelPath.string());

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
	glDisable(GL_CULL_FACE);

	thumbnailShader.UseShader();
	
	// Auto-frame with robust camera
	glm::vec3 minB = tempModel.GetMinBound();
	glm::vec3 maxB = tempModel.GetMaxBound();
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
	glUniform1i(glGetUniformLocation(thumbnailShader.GetShaderID(), "hasTexture"), tempModel.HasTextures() ? 1 : 0); 

	tempModel.RenderModel(-1, 0);

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

	// Restore full GL state
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	glUseProgram(oldProgram);
	if (oldCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (oldDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);

	tempModel.ClearModel();
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
	glUniform1i(glGetUniformLocation(thumbnailShader.GetShaderID(), "hasTexture"), 0);

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
					Texture* tex = new Texture(pStr.data());
					if (tex->LoadTexture()) {
						assetTextureCache[pStr] = tex;
						info.thumbnail = tex;
					}
					else {
						delete tex;
						info.thumbnail = nullptr;
					}
				}
			}
			else if (ext == ".obj" || ext == ".fbx" || ext == ".dae") {
				info.type = AssetType::Model;
				std::string pStr = entry.path().string();
				if (assetTextureCache.count(pStr)) {
					info.thumbnail = assetTextureCache[pStr];
				}
				else {
					Texture* tex = new Texture();
					GenerateModelThumbnail(entry.path(), tex);
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

void AssetBrowser::Render(SceneManager& scene, bool* p_open, bool forceLayout)
{
	if (p_open && !*p_open) return;
	if (!p_open && !isOpen) return;

	bool* activeOpen = p_open ? p_open : &isOpen;

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);
	
	ImGuiCond layoutCond = forceLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	ImGui::SetNextWindowPos(ImVec2(0, (float)bufferHeight * 0.7f), layoutCond);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth, (float)bufferHeight * 0.3f), layoutCond);

	if (ImGui::Begin("Project", activeOpen))
	{
		if (ImGui::Button("Refresh")) {
			RefreshAssetList();
		}
		ImGui::SameLine();
		if (ImGui::Button("..")) {
			if (currentAssetPath.has_parent_path() && currentAssetPath != "Assets") {
				currentAssetPath = currentAssetPath.parent_path();
				RefreshAssetList();
			}
		}
		ImGui::SameLine();
		ImGui::Text("Path: %s", currentAssetPath.string().c_str());

		ImGui::Separator();

		float cellSize = 100.0f;
		float padding = 16.0f;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int columnCount = (int)(panelWidth / (cellSize + padding));
		if (columnCount < 1) columnCount = 1;

		ImGui::Columns(columnCount, 0, false);

		for (int i = 0; i < (int)currentAssets.size(); i++)
		{
			ImGui::PushID(i);
			
			ImVec4 tint = ImVec4(1, 1, 1, 1);
			if (currentAssets[i].type == AssetType::Folder) tint = ImVec4(1, 0.8f, 0.4f, 1);

			ImVec2 startPos = ImGui::GetCursorPos();
			bool isSelected = (currentAssets[i].path == selectedAssetPath);

			if (ImGui::Selectable("##selectable", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cellSize, cellSize + 40))) {
				selectedAssetPath = currentAssets[i].path;
				
				if (ImGui::IsMouseDoubleClicked(0)) {
					if (currentAssets[i].type == AssetType::Folder) {
						currentAssetPath = currentAssets[i].path;
						RefreshAssetList();
						ImGui::PopID();
						break; 
					}
				}
			}

			// Drag-drop source
			if (ImGui::BeginDragDropSource()) {
				std::string pathStr = currentAssets[i].path.string();
				
				// Use different payload type for materials vs models
				const char* payloadType = (currentAssets[i].type == AssetType::MaterialAsset) ? "MATERIAL_PATH" : "ASSET_PATH";
				ImGui::SetDragDropPayload(payloadType, pathStr.c_str(), pathStr.size() + 1);
				
				ImGui::Text("Dragging %s", currentAssets[i].name.c_str());
				if (currentAssets[i].thumbnail) {
					ImGui::Image((ImTextureID)(intptr_t)currentAssets[i].thumbnail->GetTextureID(), ImVec2(32, 32), ImVec2(0, 1), ImVec2(1, 0));
				}
				ImGui::EndDragDropSource();
			}

			ImGui::SetCursorPos(startPos);
			ImGui::BeginGroup();
			
			if (currentAssets[i].thumbnail) {
				ImGui::ImageWithBg((ImTextureID)(intptr_t)currentAssets[i].thumbnail->GetTextureID(), ImVec2(cellSize, cellSize), ImVec2(0, 1), ImVec2(1, 0), ImVec4(0, 0, 0, 0), tint);
			}
			else {
				ImGui::Button("??", ImVec2(cellSize, cellSize));
			}

			ImGui::TextWrapped("%s", currentAssets[i].name.c_str());
			ImGui::EndGroup();
			
			ImGui::NextColumn();
			ImGui::PopID();
		}

		ImGui::Columns(1);
	}
	ImGui::End();
}
