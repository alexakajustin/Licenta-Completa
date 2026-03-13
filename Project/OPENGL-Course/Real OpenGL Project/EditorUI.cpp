#include "EditorUI.h"
#include "SceneManager.h"
#include "LightObject.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "Material.h"
#include "Camera.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>

EditorUI::EditorUI()
{
}

EditorUI::~EditorUI()
{
	if (previewFBO) glDeleteFramebuffers(1, &previewFBO);
	if (previewTexture) glDeleteTextures(1, &previewTexture);
	if (previewDepth) glDeleteRenderbuffers(1, &previewDepth);
	if (previewSphere) { previewSphere->ClearMesh(); delete previewSphere; }
}

void EditorUI::InitMaterialPreview()
{
	// Create FBO
	glGenFramebuffers(1, &previewFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, previewFBO);

	glGenTextures(1, &previewTexture);
	glBindTexture(GL_TEXTURE_2D, previewTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PREVIEW_SIZE, PREVIEW_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, previewTexture, 0);

	glGenRenderbuffers(1, &previewDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, previewDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, PREVIEW_SIZE, PREVIEW_SIZE);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, previewDepth);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Shader
	previewShader.CreateFromFiles("Shaders/materialPreview.vert", "Shaders/materialPreview.frag");

	// Sphere mesh
	previewSphere = PrimitiveGenerator::CreateSphere(32, 32);

	previewInitialized = true;
}

void EditorUI::RenderMaterialPreview(float specular, float shininess, glm::vec3 color, Texture* diffuse, Texture* normal)
{
	if (!previewInitialized) InitMaterialPreview();
	if (!previewSphere || previewShader.GetShaderID() == 0) return;

	// Save GL state
	GLint oldFBO, oldViewport[4], oldProgram;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
	GLboolean oldCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean oldDepthTest = glIsEnabled(GL_DEPTH_TEST);

	// Render sphere to FBO
	glBindFramebuffer(GL_FRAMEBUFFER, previewFBO);
	glViewport(0, 0, PREVIEW_SIZE, PREVIEW_SIZE);
	glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	previewShader.UseShader();
	GLuint shaderID = previewShader.GetShaderID();

	glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
	glm::mat4 model = glm::mat4(1.0f);

	glUniformMatrix4fv(previewShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(proj));
	glUniformMatrix4fv(previewShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(previewShader.GetModelLocation(), 1, GL_FALSE, glm::value_ptr(model));

	glUniform1f(glGetUniformLocation(shaderID, "specularIntensity"), specular);
	glUniform1f(glGetUniformLocation(shaderID, "shininess"), shininess);
	glUniform3f(glGetUniformLocation(shaderID, "materialColor"), color.r, color.g, color.b);

	// Bind diffuse texture
	bool hasDiff = (diffuse != nullptr && diffuse->GetTextureID() != 0);
	glUniform1i(glGetUniformLocation(shaderID, "hasDiffuse"), hasDiff ? 1 : 0);
	glUniform1i(glGetUniformLocation(shaderID, "diffuseMap"), 0);
	if (hasDiff) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, diffuse->GetTextureID());
	}

	// Bind normal map
	bool hasNorm = (normal != nullptr && normal->GetTextureID() != 0);
	glUniform1i(glGetUniformLocation(shaderID, "hasNormal"), hasNorm ? 1 : 0);
	glUniform1i(glGetUniformLocation(shaderID, "normalMap"), 1);
	if (hasNorm) {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, normal->GetTextureID());
	}

	previewSphere->RenderMesh();

	// Restore GL state
	glActiveTexture(GL_TEXTURE0);
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	glUseProgram(oldProgram);
	if (oldCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (oldDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void EditorUI::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float speed)
{
	ImGui::PushID(label.c_str());

	ImGui::Text(label.c_str());
	
	float totalWidth = ImGui::GetContentRegionAvail().x;
	float inputWidth = (totalWidth - 20.0f) / 3.0f;
	
	ImGui::SameLine(70.0f);
	ImGui::PushItemWidth(inputWidth);
	
	// X
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
	ImGui::DragFloat("##X", &values.x, speed, 0.0f, 0.0f, "X:%.2f");
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) values.x = resetValue;
	
	ImGui::SameLine(0, 5);
	
	// Y
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
	ImGui::DragFloat("##Y", &values.y, speed, 0.0f, 0.0f, "Y:%.2f");
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) values.y = resetValue;
	
	ImGui::SameLine(0, 5);
	
	// Z
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 1.0f, 1.0f));
	ImGui::DragFloat("##Z", &values.z, speed, 0.0f, 0.0f, "Z:%.2f");
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) values.z = resetValue;

	ImGui::PopItemWidth();
	ImGui::PopID();
}

void EditorUI::HandleAssetDrop(SceneManager& scene, glm::vec3 spawnPos)
{
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
		const char* pathStr = (const char*)payload->Data;
		std::filesystem::path path(pathStr);
		std::string ext = path.extension().string();
		for (auto& c : ext) c = tolower(c);

		if (ext == ".obj" || ext == ".fbx" || ext == ".dae") {
			scene.InstantiateModel(path, spawnPos);
		}
	}
}

void EditorUI::Render(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint sceneTextureID, Camera* camera)
{
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);

	RenderHierarchy(scene, bufferHeight, camera);
	RenderInspector(scene, bufferWidth, bufferHeight);
}

void EditorUI::RenderViewport(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint textureID)
{
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);

	ImGui::SetNextWindowPos(ImVec2((float)bufferWidth * 0.2f, 0), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth * 0.5f, (float)bufferHeight * 0.7f), ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (ImGui::Begin("Scene", &windowState.isViewportOpen))
	{
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		
		// Render the scene texture
		// Need to flip V because OpenGL textures are bottom-up
		ImGui::Image((ImTextureID)(intptr_t)textureID, viewportPanelSize, ImVec2(0, 1), ImVec2(1, 0));

		// Drag and Drop support for viewport
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
				const char* pathStr = (const char*)payload->Data;
				std::filesystem::path path(pathStr);
				std::string ext = path.extension().string();
				for (auto& c : ext) c = tolower(c);

				if (ext == ".obj" || ext == ".fbx" || ext == ".dae") {
					// Calculate world position from mouse ray
					ImVec2 mousePos = ImGui::GetMousePos();
					ImVec2 winPos = ImGui::GetWindowPos();
					ImVec2 winSize = ImGui::GetWindowSize();
					glm::vec3 rayDir = scene.GetMouseRay(mousePos.x - winPos.x, mousePos.y - winPos.y, projection, view, winSize.x, winSize.y);
					glm::vec3 spawnPos(0.0f);
					if (!scene.RayPlaneIntersect(cameraPos, rayDir, glm::vec3(0,0,0), glm::vec3(0,1,0), spawnPos)) {
						spawnPos = cameraPos + rayDir * 5.0f;
					}
					scene.InstantiateModel(path, spawnPos);
				}
				else if (ext == ".mat") {
					// Material drop: apply to object under mouse
					ImVec2 mousePos = ImGui::GetMousePos();
					ImVec2 winPos = ImGui::GetWindowPos();
					ImVec2 winSize = ImGui::GetWindowSize();
					int pickedID = scene.PickObject(mousePos.x - winPos.x, mousePos.y - winPos.y, projection, view, cameraPos, winSize.x, winSize.y);
					if (pickedID >= 0 && pickedID < (int)scene.GetObjects().size()) {
						Material* loadedMat = Material::LoadFromFile(pathStr);
						if (loadedMat) {
							scene.GetObjects()[pickedID]->SetMaterial(loadedMat);
							scene.SetSelectedIndex(pickedID);
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

void EditorUI::RenderHierarchy(SceneManager& scene, int bufferHeight, Camera* camera)
{
	int bufferWidth;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, nullptr);

	// Hierarchy on the left
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth * 0.3f, (float)bufferHeight * 0.7f), ImGuiCond_FirstUseEver);
	if (!windowState.isHierarchyOpen) return;

	if (ImGui::Begin("Scene Hierarchy", &windowState.isHierarchyOpen))
	{
		auto& objects = scene.GetObjects();
		auto& lights = scene.GetLights();

		if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Drop target for reparenting to ROOT
			ImGui::Selectable("##RootDropTarget", false, ImGuiSelectableFlags_Disabled, ImVec2(0, 5));
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
					int draggedIdx = *(const int*)payload->Data;
					if (draggedIdx >= 0 && draggedIdx < (int)objects.size()) {
						objects[draggedIdx]->SetParent(nullptr);
					}
				}
				ImGui::EndDragDropTarget();
			}

			for (int i = 0; i < (int)objects.size(); i++)
			{
				// Only start recursive draw from roots
				if (objects[i]->GetParent() == nullptr) {
					RenderHierarchyRecursive(scene, objects[i], i, camera);
				}
			}
		}

		if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int i = 0; i < (int)lights.size(); i++)
			{
				ImGui::PushID(1000 + i);
				const char* icon = (lights[i]->GetLightType() == LightType::Directional) ? "[D] " : 
								   (lights[i]->GetLightType() == LightType::Point) ? "[P] " : "[S] ";
				std::string label = std::string(icon) + lights[i]->GetName();
				bool isSelected = scene.IsLightSelected(i);
				if (ImGui::Selectable(label.c_str(), isSelected))
				{
					bool multiSelect = ImGui::GetIO().KeyCtrl;
					bool rangeSelect = ImGui::GetIO().KeyShift;
					scene.SetSelectedLightIndex(i, multiSelect, rangeSelect);
				}

				// Double-click to focus
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && camera) {
					glm::vec3* lp = lights[i]->GetPositionPtr();
					if (lp) camera->SetPositionAndLookAt(*lp);
				}

				ImGui::PopID();
			}
		}

		// Delete key — only when this window is focused
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			auto& selObjects = scene.GetSelectedObjectIndices();
			auto& selLights = scene.GetSelectedLightIndices();

			if (!selObjects.empty()) {
				scene.DeleteSelectedObjects();
			}
			else if (!selLights.empty()) {
				scene.DeleteSelectedLights();
			}
		}

		// Right-click context menu
		if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight))
		{
			if (ImGui::BeginMenu("Create GameObject"))
			{
				if (ImGui::MenuItem("Plane")) scene.CreateGameObject("Plane");
				if (ImGui::MenuItem("Cube")) scene.CreateGameObject("Cube");
				if (ImGui::MenuItem("Sphere")) scene.CreateGameObject("Sphere");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Create Light"))
			{
				if (ImGui::MenuItem("Point Light")) scene.CreateLight(LightType::Point);
				if (ImGui::MenuItem("Spot Light")) scene.CreateLight(LightType::Spot);
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		// Drag-drop target (DRY: uses shared handler)
		if (ImGui::BeginDragDropTarget()) {
			HandleAssetDrop(scene, glm::vec3(0.0f)); // Pass a dummy spawnPos, as it's not used for material drops
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::End();
}

void EditorUI::RenderHierarchyRecursive(SceneManager& scene, GameObject* obj, int index, Camera* camera)
{
	if (!obj) return;

	ImGui::PushID(index);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (scene.IsObjectSelected(index)) flags |= ImGuiTreeNodeFlags_Selected;
	if (obj->GetChildren().empty()) flags |= ImGuiTreeNodeFlags_Leaf;

	bool isNodeOpen = ImGui::TreeNodeEx(obj->GetName().c_str(), flags);

	// Selection logic
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		bool multiSelect = ImGui::GetIO().KeyCtrl;
		bool rangeSelect = ImGui::GetIO().KeyShift;
		scene.SetSelectedIndex(index, multiSelect, rangeSelect);
	}

	// Double-click to focus
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && camera) {
		// Use world position for focus
		glm::vec3 worldPos = glm::vec3(obj->GetWorldMatrix()[3]);
		camera->SetPositionAndLookAt(worldPos);
	}

	// --- Drag Source ---
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("SCENE_OBJECT_INDEX", &index, sizeof(int));
		ImGui::Text("Dragging %s", obj->GetName().c_str());
		ImGui::EndDragDropSource();
	}

	// --- Drag Target (Reparenting) ---
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
			int draggedIdx = *(const int*)payload->Data;
			auto& objects = scene.GetObjects();
			if (draggedIdx >= 0 && draggedIdx < (int)objects.size()) {
				GameObject* draggedObj = objects[draggedIdx];

				// Prevent cycle: check if 'obj' is a descendant of 'draggedObj'
				bool isDescendant = false;
				GameObject* p = obj;
				while (p) {
					if (p == draggedObj) { isDescendant = true; break; }
					p = p->GetParent();
				}

				if (!isDescendant && draggedObj != obj) {
					draggedObj->SetParent(obj);
				}
			}
		}

		// Accept material drops
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_PATH")) {
			const char* matPath = (const char*)payload->Data;
			Material* mat = Material::LoadFromFile(matPath);
			if (mat) obj->SetMaterial(mat);
		}

		ImGui::EndDragDropTarget();
	}

	if (isNodeOpen) {
		for (auto* child : obj->GetChildren()) {
			// Find child index in the global objects list for selection
			int childIndex = -1;
			auto& allObjs = scene.GetObjects();
			for (int j = 0; j < (int)allObjs.size(); j++) {
				if (allObjs[j] == child) { childIndex = j; break; }
			}

			if (childIndex != -1) {
				RenderHierarchyRecursive(scene, child, childIndex, camera);
			}
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void EditorUI::RenderInspector(SceneManager& scene, int bufferWidth, int bufferHeight)
{
	auto& objects = scene.GetObjects();
	auto& lights = scene.GetLights();
	int selectedObj = scene.GetSelectedIndex();
	int selectedLight = scene.GetSelectedLightIndex();

	bool showObjectInspector = (selectedObj >= 0 && selectedLight < 0);
	bool showLightInspector = (selectedLight >= 0 && selectedObj < 0);

	// Inspector - Usually tabbed with hierarchy or on the far side. 
	// For this layout, we'll place it as a float or tabbed if needed, but for now let's prioritize the 3-column top
	ImGui::SetNextWindowPos(ImVec2((float)bufferWidth - 300.0f, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, (float)bufferHeight * 0.7f), ImGuiCond_FirstUseEver);
	if (!windowState.isInspectorOpen) return;

	if (!showObjectInspector && !showLightInspector) {
		// Even if empty, show a blank Inspector window for docking consistency
		if (ImGui::Begin("Inspector", &windowState.isInspectorOpen)) {
			ImGui::TextDisabled("Select an object to inspect");
		}
		ImGui::End();
		return;
	}

	if (ImGui::Begin("Inspector", &windowState.isInspectorOpen))
	{
		if (showObjectInspector)
		{
			GameObject* selected = objects[selectedObj];
			Transform& transform = selected->GetTransform();
			char nameBuf[128];
			strncpy_s(nameBuf, sizeof(nameBuf), selected->GetName().c_str(), _TRUNCATE);
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			{
				selected->SetName(nameBuf);
			}
			ImGui::Separator();

			// --- Transform (collapsible) ---
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawVec3Control("Position", *transform.GetPositionPtr(), 0.0f, 0.1f);
				DrawVec3Control("Rotation", *transform.GetRotationPtr(), 0.0f, 1.0f);
				DrawVec3Control("Scale", *transform.GetScalePtr(), 1.0f, 0.01f);
			}

			// --- Textures (collapsible, with preview squares + drag-drop) ---
			if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
			{
				float squareSize = 64.0f;

				// Diffuse Map
				ImGui::Text("Diffuse Map");
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - squareSize);
				Texture* diffuse = selected->GetTexture();
				if (diffuse && diffuse->GetTextureID() != 0) {
					ImGui::Image((ImTextureID)(intptr_t)diffuse->GetTextureID(), ImVec2(squareSize, squareSize), ImVec2(0, 1), ImVec2(1, 0));
				} else {
					ImGui::Button("None##Diffuse", ImVec2(squareSize, squareSize));
				}
				// Drop texture onto diffuse slot
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
						const char* pathStr = (const char*)payload->Data;
						std::filesystem::path path(pathStr);
						std::string ext = path.extension().string();
						for (auto& c : ext) c = tolower(c);
						if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
							Texture* newTex = new Texture(pathStr);
							if (newTex->LoadTextureA()) {
								selected->SetTexture(newTex);
								printf("Applied diffuse: %s\n", pathStr);
							} else { delete newTex; }
						}
					}
					ImGui::EndDragDropTarget();
				}
				if (diffuse) { ImGui::SameLine(); if (ImGui::SmallButton("X##ClearDiffuse")) selected->SetTexture(nullptr); }

				ImGui::Spacing();

				// Normal Map
				ImGui::Text("Normal Map");
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - squareSize);
				Texture* normal = selected->GetNormalMap();
				if (normal && normal->GetTextureID() != 0) {
					ImGui::Image((ImTextureID)(intptr_t)normal->GetTextureID(), ImVec2(squareSize, squareSize), ImVec2(0, 1), ImVec2(1, 0));
				} else {
					ImGui::Button("None##Normal", ImVec2(squareSize, squareSize));
				}
				// Drop texture onto normal slot
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
						const char* pathStr = (const char*)payload->Data;
						std::filesystem::path path(pathStr);
						std::string ext = path.extension().string();
						for (auto& c : ext) c = tolower(c);
						if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
							Texture* newTex = new Texture(pathStr);
							if (newTex->LoadTextureA()) {
								selected->SetNormalMap(newTex);
								printf("Applied normal map: %s\n", pathStr);
							} else { delete newTex; }
						}
					}
					ImGui::EndDragDropTarget();
				}
				if (normal) { ImGui::SameLine(); if (ImGui::SmallButton("X##ClearNormal")) selected->SetNormalMap(nullptr); }
			}

			// --- Material (collapsible, with preview sphere) ---
			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
			{
				Material* mat = selected->GetMaterial();
				
				if (mat) {
					// Editable properties
					float specular = mat->GetSpecularIntensity();
					float shininess = mat->GetShininess();
					
					if (ImGui::SliderFloat("Specular", &specular, 0.0f, 4.0f)) {
						mat->SetSpecularIntensity(specular);
					}
					if (ImGui::SliderFloat("Shininess", &shininess, 1.0f, 256.0f, "%.0f")) {
						mat->SetShininess(shininess);
					}

					glm::vec3 matCol = mat->GetColor();
					if (ImGui::ColorEdit3("Base Color", &matCol.x)) {
						mat->SetColor(matCol);
					}
					
					// Live preview sphere (shows diffuse + normal + material properties)
					RenderMaterialPreview(specular, shininess, matCol, selected->GetTexture(), selected->GetNormalMap());
					if (previewTexture) {
						ImGui::Image((ImTextureID)(intptr_t)previewTexture, ImVec2(PREVIEW_SIZE, PREVIEW_SIZE), ImVec2(0, 1), ImVec2(1, 0));
					}
					if (ImGui::Button("X##ClearMaterial")) selected->SetMaterial(nullptr);
				} else {
					ImGui::TextDisabled("No Material");
					if (ImGui::Button("Add Material")) {
						Material* newMat = new Material(0.5f, 32.0f);
						selected->SetMaterial(newMat);
					}
				}
				
				// Accept material drop on this section
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_PATH")) {
						const char* matPath = (const char*)payload->Data;
						Material* loadedMat = Material::LoadFromFile(matPath);
						if (loadedMat) {
							selected->SetMaterial(loadedMat);
							printf("Applied material %s\n", matPath);
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}
		else if (showLightInspector)
		{
			LightObject* light = lights[selectedLight];
			char nameBuf[128];
			strncpy_s(nameBuf, sizeof(nameBuf), light->GetName().c_str(), _TRUNCATE);
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			{
				light->SetName(nameBuf);
			}
			ImGui::Separator();
			ImGui::ColorEdit3("Color", &light->GetColorPtr()->x);
			ImGui::SliderFloat("Ambient", light->GetAmbientIntensityPtr(), 0.0f, 1.0f);
			ImGui::SliderFloat("Diffuse", light->GetDiffuseIntensityPtr(), 0.0f, 2.0f);
			
			if (light->GetPositionPtr()) DrawVec3Control("Position", *light->GetPositionPtr(), 0.0f, 0.1f);
			if (light->GetDirectionPtr()) DrawVec3Control("Direction", *light->GetDirectionPtr(), 0.0f, 0.01f);
			
			if (light->GetConstantPtr()) {
				ImGui::Separator();
				ImGui::Text("Attenuation");
				ImGui::SliderFloat("Constant", light->GetConstantPtr(), 0.01f, 2.0f);
				ImGui::SliderFloat("Linear", light->GetLinearPtr(), 0.001f, 0.5f);
				ImGui::SliderFloat("Exponent", light->GetExponentPtr(), 0.001f, 0.5f);
			}
		}

		// Drag-drop target for models (whole inspector area)
		if (ImGui::BeginDragDropTarget()) {
			HandleAssetDrop(scene);
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::End();
}

// RenderViewport renamed to Render (which handles the Scene window)
// RenderViewportDropTarget removed as it's now handled by the Scene window logic

