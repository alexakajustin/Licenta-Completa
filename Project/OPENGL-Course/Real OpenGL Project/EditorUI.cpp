#include "EditorUI.h"
#include "Application.h"
#include "InputHandler.h"
#include "External Libs/ImGUI/imgui_internal.h"

#include "SceneManager.h"
#include "LightObject.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "Material.h"
#include "Camera.h"
#include "NodeGraph.h"
#include "SceneInputNode.h"
#include "PerlinNoiseNode.h"
#include "ScatterNode.h"
#include "FilterTransformListNode.h"
#include "MergeMeshNode.h"
#include "OutputNode.h"
#include "HydraulicErosionNode.h"
#include "RiverNode.h"
#include "CityGridNode.h"
#include "BuildingGenNode.h"
#include "BeautifulErosionNode.h"
#include "SceneSerializer.h"
#include "UndoActions.h"
#include "Planet.h"
#include <filesystem>
#include <set>
#include <cstring>

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

void EditorUI::RenderMaterialPreview(float specular, float shininess, glm::vec3 color, Texture* diffuse, Texture* normal, glm::vec2 tiling, glm::vec2 offset)
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
	
	glUniform2f(glGetUniformLocation(shaderID, "tiling"), tiling.x, tiling.y);
	glUniform2f(glGetUniformLocation(shaderID, "offset"), offset.x, offset.y);

	previewSphere->RenderMesh();

	// Restore GL state
	glActiveTexture(GL_TEXTURE0);
	glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	glUseProgram(oldProgram);
	if (oldCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (oldDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void EditorUI::DrawVec2Control(const std::string& label, float& v1, float& v2, const std::string& label1, const std::string& label2, float resetValue, float speed)
{
	ImGui::PushID(label.c_str());

	ImGui::Text(label.c_str());
	ImGui::SameLine(70.0f);

	float totalWidth = ImGui::GetContentRegionAvail().x;
	float inputWidth = (totalWidth - 5.0f) / 2.0f;

	ImGui::PushItemWidth(inputWidth);

	// Val 1
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
	std::string fmt1 = label1 + ":%.2f";
	ImGui::DragFloat("##V1", &v1, speed, 0.0f, 0.0f, fmt1.c_str());
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) v1 = resetValue;

	ImGui::SameLine(0, 5);

	// Val 2
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
	std::string fmt2 = label2 + ":%.2f";
	ImGui::DragFloat("##V2", &v2, speed, 0.0f, 0.0f, fmt2.c_str());
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(1)) v2 = resetValue;

	ImGui::PopItemWidth();
	ImGui::PopID();
}

void EditorUI::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float speed)
{
	ImGui::PushID(label.c_str());

	ImGui::Text(label.c_str());
	ImGui::SameLine(70.0f);
	
	// Calculate width AFTER indenting, so elements fit perfectly without overflowing
	float totalWidth = ImGui::GetContentRegionAvail().x;
	float inputWidth = (totalWidth - 10.0f) / 3.0f; // 2 spacing gaps of 5px
	
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
	const ImGuiPayload* payload = nullptr;
	bool isAsset = false;
	bool isMaterial = false;

	if (payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) isAsset = true;
	else if (payload = ImGui::AcceptDragDropPayload("MATERIAL_PATH")) isMaterial = true;

	if (payload) {
		const char* pathStr = (const char*)payload->Data;
		std::filesystem::path path(pathStr);
		std::string ext = path.extension().string();
		for (auto& c : ext) c = tolower(c);

		if (isAsset && (ext == ".obj" || ext == ".fbx" || ext == ".dae")) {
			scene.InstantiateModel(path, spawnPos);
		}
		else if (isMaterial || ext == ".mat") {
			// If we dropped on a specific object window (handled by Hierarchy/Inspector), 
			// it usually handles the application itself. This global helper is for 
			// general instantiation or "current selection" application.
			Material* loadedMat = Material::LoadFromFile(pathStr);
			if (loadedMat) {
				int sel = scene.GetSelectedIndex();
				if (sel >= 0 && sel < (int)scene.GetObjects().size()) {
					scene.GetObjects()[sel]->SetMaterial(loadedMat);
				}
			}
		}
	}
}

void EditorUI::WindowState::CheckMaximize(int windowID)
{
	// Detect double click on title bar
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		ImVec2 mousePos = ImGui::GetIO().MousePos;
		ImVec2 windowPos = ImGui::GetWindowPos();
		float titleBarHeight = ImGui::GetTextLineHeightWithSpacing();

		// Check if click was in the title bar area
		if (mousePos.y >= windowPos.y && mousePos.y <= windowPos.y + titleBarHeight)
		{
			if (maximizedWindowID == windowID)
				maximizedWindowID = -1; // Minimize
			else
				maximizedWindowID = windowID; // Maximize
		}
	}
}


void EditorUI::UpdateLayoutLogic()
{
	if (ImGui::GetIO().DisplaySize.x <= 0) return;
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) return; // Disable layout changes while scene is focused
	if (windowState.maximizedWindowID != -1) return; // Disable splitters while maximized
	if (ImGui::IsDragDropActive()) { // Disable splitters while dragging items
		windowState.activeSplitterID = -1;
		return;
	}

	// Universal Fix: If any ImGui widget is active (slider, scrollbar, etc.), ignore splitters
	if (ImGui::IsAnyItemActive() && windowState.activeSplitterID == -1) return;

	ImVec2 mousePos = ImGui::GetIO().MousePos;
	bool mousePressed = ImGui::IsMouseDown(0);
	float winWidth = ImGui::GetIO().DisplaySize.x;
	float winHeight = ImGui::GetIO().DisplaySize.y;
	float menuHeight = ImGui::GetFrameHeight(); // Use accurate frame height
	float contentHeight = winHeight - menuHeight;

	// Occlusion check: Are we hovering over a floating window (not tiled)?
	ImGuiWindow* hovered = GImGui->HoveredWindow;
	bool occluded = false;
	if (hovered != nullptr) {
		const char* name = hovered->Name;
		bool isTiled = (strcmp(name, "Scene") == 0 || strcmp(name, "Scene Hierarchy") == 0 || 
					    strcmp(name, "Inspector") == 0 || strcmp(name, "Project") == 0 || 
					    strcmp(name, "Node Editor") == 0 || strcmp(name, "CPU Debug") == 0 ||
						strcmp(name, "Scene Viewport") == 0); // Include internal names if any
		
		// If hovering something that isn't a tiled panel, it occludes the splitters
		if (!isTiled) occluded = true;
	}



	// Handle Dragging
	if (windowState.activeSplitterID != -1) {
		if (!mousePressed) {
			windowState.activeSplitterID = -1;
		} else {
			if (windowState.activeSplitterID == 0) { // SplitX1
				windowState.leftWidth = mousePos.x;
				float minX = windowState.minPanelWidth;
				float maxX = winWidth - windowState.rightWidth - windowState.minPanelWidth;
				if (windowState.leftWidth < minX) windowState.leftWidth = minX;
				if (windowState.leftWidth > maxX) windowState.leftWidth = maxX;
			}
			else if (windowState.activeSplitterID == 1) { // SplitX2
				float rightX = mousePos.x;
				float minX = windowState.leftWidth + windowState.minPanelWidth;
				float maxX = winWidth - windowState.minPanelWidth;
				if (rightX < minX) rightX = minX;
				if (rightX > maxX) rightX = maxX;
				windowState.rightWidth = winWidth - rightX;
			}
			else if (windowState.activeSplitterID == 2) { // SplitY_L
				windowState.leftHeightRatio = (mousePos.y - menuHeight) / contentHeight;
				float minY = windowState.minPanelHeight / contentHeight;
				float maxY = 1.0f - minY;
				if (windowState.leftHeightRatio < minY) windowState.leftHeightRatio = minY;
				if (windowState.leftHeightRatio > maxY) windowState.leftHeightRatio = maxY;
			}
			else if (windowState.activeSplitterID == 3) { // SplitY_M
				windowState.midHeightRatio = (mousePos.y - menuHeight) / contentHeight;
				float minY = windowState.minPanelHeight / contentHeight;
				float maxY = 1.0f - minY;
				if (windowState.midHeightRatio < minY) windowState.midHeightRatio = minY;
				if (windowState.midHeightRatio > maxY) windowState.midHeightRatio = maxY;
			}
			else if (windowState.activeSplitterID == 4) { // SplitY_R
				windowState.rightHeightRatio = (mousePos.y - menuHeight) / contentHeight;
				float minY = windowState.minPanelHeight / contentHeight;
				float maxY = 1.0f - minY;
				if (windowState.rightHeightRatio < minY) windowState.rightHeightRatio = minY;
				if (windowState.rightHeightRatio > maxY) windowState.rightHeightRatio = maxY;
			}
		}
	}

	// Handle Hit-Testing (only if not already dragging and NOT occluded)
	if (windowState.activeSplitterID == -1 && mousePressed && !occluded) {
		float hitBuffer = 2.0f; // Don't allow hits right at the menu border

		// SplitX1
		if (abs(mousePos.x - windowState.leftWidth) < 5.0f && mousePos.y > menuHeight + hitBuffer) windowState.activeSplitterID = 0;
		// SplitX2
		else if (abs(mousePos.x - (winWidth - windowState.rightWidth)) < 5.0f && mousePos.y > menuHeight + hitBuffer) windowState.activeSplitterID = 1;
		// SplitY_L (Left Column)
		else if (mousePos.x < windowState.leftWidth && abs(mousePos.y - (menuHeight + contentHeight * windowState.leftHeightRatio)) < 5.0f) windowState.activeSplitterID = 2;
		// SplitY_M (Middle Column)
		else if (mousePos.x > windowState.leftWidth && mousePos.x < (winWidth - windowState.rightWidth) && 
				 abs(mousePos.y - (menuHeight + contentHeight * windowState.midHeightRatio)) < 5.0f) windowState.activeSplitterID = 3;
		// SplitY_R (Right Column)
		else if (mousePos.x > (winWidth - windowState.rightWidth) && 
				 abs(mousePos.y - (menuHeight + contentHeight * windowState.rightHeightRatio)) < 5.0f) windowState.activeSplitterID = 4;
	}

}

void EditorUI::UpdateLayoutVisual()
{
	if (windowState.maximizedWindowID != -1) return; // Hide splitters while maximized
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) return; // Hide everything while scene is focused
	if (ImGui::IsDragDropActive()) return; // Hide splitters while dragging items
	if (ImGui::IsAnyItemActive() && windowState.activeSplitterID == -1) return; // Hide while interacting with widgets

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winWidth = displaySize.x;
	float winHeight = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();
	float contentHeight = winHeight - menuHeight;
	ImVec2 mousePos = ImGui::GetIO().MousePos;
	ImDrawList* fgDraw = ImGui::GetForegroundDrawList();

	float hitBuffer = 2.0f; // Visual safe zone

	// Occlusion check for visuals
	ImGuiWindow* hoveredWindow = GImGui->HoveredWindow;
	bool occluded = false;
	if (hoveredWindow != nullptr) {
		const char* name = hoveredWindow->Name;
		bool isTiled = (strcmp(name, "Scene") == 0 || strcmp(name, "Scene Hierarchy") == 0 || 
					    strcmp(name, "Inspector") == 0 || strcmp(name, "Project") == 0 || 
					    strcmp(name, "Node Editor") == 0 || strcmp(name, "CPU Debug") == 0);
		if (!isTiled) occluded = true;
	}

	// Cursor and Highlights
	int hoverID = -1;
	if (windowState.activeSplitterID == -1 && !occluded) {

		if (abs(mousePos.x - windowState.leftWidth) < 5.0f && mousePos.y > menuHeight + hitBuffer) hoverID = 0;
		else if (abs(mousePos.x - (winWidth - windowState.rightWidth)) < 5.0f && mousePos.y > menuHeight + hitBuffer) hoverID = 1;
		else if (mousePos.x < windowState.leftWidth && abs(mousePos.y - (menuHeight + contentHeight * windowState.leftHeightRatio)) < 5.0f) hoverID = 2;
		else if (mousePos.x > windowState.leftWidth && mousePos.x < (winWidth - windowState.rightWidth) && 
				 abs(mousePos.y - (menuHeight + contentHeight * windowState.midHeightRatio)) < 5.0f) hoverID = 3;
		else if (mousePos.x > (winWidth - windowState.rightWidth) && 
				 abs(mousePos.y - (menuHeight + contentHeight * windowState.rightHeightRatio)) < 5.0f) hoverID = 4;
	} else {

		hoverID = windowState.activeSplitterID;
	}

	if (hoverID == 0 || hoverID == 1) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	else if (hoverID == 2 || hoverID == 3 || hoverID == 4) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

	// Highlight Active/Hovered

	ImU32 highlightColor = ImGui::GetColorU32(windowState.activeSplitterID != -1 ? ImVec4(0.6f, 0.3f, 0.8f, 1.0f) : ImVec4(0.6f, 0.3f, 0.8f, 0.5f));
	if (hoverID == 0) fgDraw->AddRectFilled(ImVec2(windowState.leftWidth - 2, menuHeight + hitBuffer), ImVec2(windowState.leftWidth + 2, winHeight), highlightColor);
	if (hoverID == 1) fgDraw->AddRectFilled(ImVec2(winWidth - windowState.rightWidth - 2, menuHeight + hitBuffer), ImVec2(winWidth - windowState.rightWidth + 2, winHeight), highlightColor);
	if (hoverID == 2) fgDraw->AddRectFilled(ImVec2(0, menuHeight + contentHeight * windowState.leftHeightRatio - 2), ImVec2(windowState.leftWidth, menuHeight + contentHeight * windowState.leftHeightRatio + 2), highlightColor);
	if (hoverID == 3) fgDraw->AddRectFilled(ImVec2(windowState.leftWidth, menuHeight + contentHeight * windowState.midHeightRatio - 2), ImVec2(winWidth - windowState.rightWidth, menuHeight + contentHeight * windowState.midHeightRatio + 2), highlightColor);
	if (hoverID == 4) fgDraw->AddRectFilled(ImVec2(winWidth - windowState.rightWidth, menuHeight + contentHeight * windowState.rightHeightRatio - 2), ImVec2(winWidth, menuHeight + contentHeight * windowState.rightHeightRatio + 2), highlightColor);

}




void EditorUI::Render(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint sceneTextureID, Camera* camera, const InputHandler* inputHandler)
{
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);

	RenderHierarchy(scene, bufferHeight, camera);
	RenderViewport(scene, projection, view, cameraPos, sceneTextureID, camera, inputHandler);
	RenderInspector(scene, bufferWidth, bufferHeight);
}



void EditorUI::UpdateViewportMetadata()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winWidth = displaySize.x;
	float winHeight = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();

	// Scene Viewport (to update metadata)
	ImVec2 pos(windowState.leftWidth, menuHeight);
	ImVec2 size(winWidth - windowState.leftWidth - windowState.rightWidth, (winHeight - menuHeight) * windowState.midHeightRatio);

	if (windowState.maximizedWindowID == 1) { // Scene Maximized
		pos = ImVec2(0, menuHeight);
		size = ImVec2(winWidth, winHeight - menuHeight);
	} else if (windowState.maximizedWindowID != -1) { // Something ELSE is maximized
		return; // Completely skip the Scene window
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (ImGui::Begin("Scene", nullptr, windowFlags))
	{
		windowState.CheckMaximize(1); // Scene ID = 1

		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		viewportSize = glm::vec2(panelSize.x, panelSize.y);
		viewportPos = glm::vec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
		viewportHovered = ImGui::IsWindowHovered();
	}
	ImGui::End();
}


void EditorUI::RenderMainMenuBar(SceneManager& scene, NodeGraph& nodeGraph, Camera* camera)
{
	glm::vec3 spawnPos = camera ? camera->getCameraPosition() + camera->getCameraDirection() * 10.0f : glm::vec3(0.0f);
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
			{
				pendingSceneAction = SceneAction::New;
			}
			if (ImGui::MenuItem("Save Scene"))
			{
				std::string path = SceneSerializer::SaveFileDialog();
				if (!path.empty())
				{
					pendingSceneAction = SceneAction::Save;
					pendingScenePath = path;
				}
			}
			if (ImGui::MenuItem("Load Scene"))
			{
				std::string path = SceneSerializer::OpenFileDialog();
				if (!path.empty())
				{
					pendingSceneAction = SceneAction::Load;
					pendingScenePath = path;
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4")) { glfwSetWindowShouldClose(glfwGetCurrentContext(), true); }
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			std::string undoLabel = scene.GetUndoManager().CanUndo() 
				? "Undo " + scene.GetUndoManager().GetUndoDescription() 
				: "Undo";
			std::string redoLabel = scene.GetUndoManager().CanRedo() 
				? "Redo " + scene.GetUndoManager().GetRedoDescription() 
				: "Redo";

			if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, scene.GetUndoManager().CanUndo()))
			{
				scene.GetUndoManager().Undo();
			}
			if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Shift+Z", false, scene.GetUndoManager().CanRedo()))
			{
				scene.GetUndoManager().Redo();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Empty")) { scene.CreateGameObject("Empty Object", spawnPos); }
			ImGui::Separator();
			if (ImGui::MenuItem("3D Object -> Plane")) { scene.CreateGameObject("Plane", spawnPos); }
			if (ImGui::MenuItem("3D Object -> Cube")) { scene.CreateGameObject("Cube", spawnPos); }
			if (ImGui::MenuItem("3D Object -> Sphere")) { scene.CreateGameObject("Sphere", spawnPos); }
			if (ImGui::MenuItem("3D Object -> Planet")) { scene.CreateGameObject("Planet", spawnPos); }
			ImGui::Separator();
			if (ImGui::BeginMenu("Light"))
			{
				if (ImGui::MenuItem("Point Light")) { scene.CreateLight(LightType::Point, spawnPos); }
				if (ImGui::MenuItem("Spot Light")) { scene.CreateLight(LightType::Spot, spawnPos); }
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			ImGui::MenuItem("Scene Hierarchy", nullptr, &windowState.isHierarchyOpen);
			ImGui::MenuItem("Inspector", nullptr, &windowState.isInspectorOpen);
			ImGui::MenuItem("Project (Asset Browser)", nullptr, &windowState.isAssetBrowserOpen);
			ImGui::MenuItem("Node Editor", nullptr, &windowState.isNodeEditorOpen);
			ImGui::MenuItem("Node Builder", nullptr, &windowState.isNodeBuilderOpen);
			ImGui::Separator();
			ImGui::MenuItem("Debug Overlay", nullptr, &windowState.isDebugOverlayOpen);
			ImGui::Separator();
			ImGui::MenuItem("Graphics Settings", nullptr, &windowState.isGraphicsSettingsOpen);
			
			if (ImGui::BeginMenu("Layout"))
			{
				if (ImGui::MenuItem("Reset Layout"))
				{
					windowState.isHierarchyOpen = true;
					windowState.isInspectorOpen = true;
					windowState.isAssetBrowserOpen = true;
					windowState.isNodeEditorOpen = true;
					windowState.isDebugOverlayOpen = true;

					windowState.leftWidth = 260.0f;
					windowState.rightWidth = 450.0f;
					windowState.leftHeightRatio = 0.4f;
					windowState.midHeightRatio = 0.75f;
					windowState.rightHeightRatio = 0.75f;

					windowState.forceLayout = true;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Templates"))
		{
			if (ImGui::MenuItem("Procedural Terrain"))
			{
				nodeGraph.Clear();
				SceneInputNode* input = new SceneInputNode(nodeGraph);
				PerlinNoiseNode* noise = new PerlinNoiseNode(nodeGraph);
				OutputNode* output = new OutputNode(nodeGraph);

				input->editorPos = glm::vec2(50, 100);
				noise->editorPos = glm::vec2(300, 100);
				output->editorPos = glm::vec2(550, 100);

				nodeGraph.AddNode(input);
				nodeGraph.AddNode(noise);
				nodeGraph.AddNode(output);

				nodeGraph.AddLink(input->outputs[0].id, noise->inputs[0].id);
				nodeGraph.AddLink(noise->outputs[0].id, output->inputs[0].id);

				// Auto-setup: try to find "Plane"
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					if (scene.GetObjects()[i]->GetName() == "Plane") {
						input->SetSelection(i, "Plane");
						break;
					}
				}
			}

			if (ImGui::MenuItem("Distributed Nature"))
			{
				nodeGraph.Clear();
				SceneInputNode* groundInput = new SceneInputNode(nodeGraph);
				SceneInputNode* rockInput = new SceneInputNode(nodeGraph);
				ScatterNode* scatter = new ScatterNode(nodeGraph);

				groundInput->editorPos = glm::vec2(50, 50);
				rockInput->editorPos = glm::vec2(50, 250);
				scatter->editorPos = glm::vec2(300, 150);

				scatter->SetSpawnAsObjects(true);

				nodeGraph.AddNode(groundInput);
				nodeGraph.AddNode(rockInput);
				nodeGraph.AddNode(scatter);

				// Connect Scatter Surface (from ground)
				nodeGraph.AddLink(groundInput->outputs[0].id, scatter->inputs[0].id);

				// Connect Rock Input to Scatter
				nodeGraph.AddLink(rockInput->outputs[0].id, scatter->inputs[1].id);

				// Auto-setup: try to find "Plane" and "Cube 1"
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					std::string name = scene.GetObjects()[i]->GetName();
					if (name == "Plane") {
						groundInput->SetSelection(i, "Plane");
					}
					if (name == "Cube 1") {
						rockInput->SetSelection(i, "Cube 1");
					}
				}
			}

			if (ImGui::MenuItem("Filtered Scattering"))
			{
				nodeGraph.Clear();
				SceneInputNode* groundInput = new SceneInputNode(nodeGraph);
				SceneInputNode* objectInput = new SceneInputNode(nodeGraph);
				ScatterNode* scatter = new ScatterNode(nodeGraph);
				FilterTransformListNode* filter = new FilterTransformListNode(nodeGraph);

				groundInput->editorPos = glm::vec2(50, 50);
				objectInput->editorPos = glm::vec2(50, 250);
				scatter->editorPos = glm::vec2(250, 150);
				filter->editorPos = glm::vec2(500, 150);

				scatter->SetSpawnAsObjects(true);

				nodeGraph.AddNode(groundInput);
				nodeGraph.AddNode(objectInput);
				nodeGraph.AddNode(scatter);
				nodeGraph.AddNode(filter);

				// Connect Scatter Pipeline
				nodeGraph.AddLink(groundInput->outputs[0].id, scatter->inputs[0].id); // Surface
				nodeGraph.AddLink(objectInput->outputs[0].id, scatter->inputs[1].id); // Object

				// Connect Filter Pipeline
				nodeGraph.AddLink(scatter->outputs[1].id, filter->inputs[0].id); // Instances -> Filter

				// Auto-setup defaults
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					std::string name = scene.GetObjects()[i]->GetName();
					if (name == "Plane") {
						groundInput->SetSelection(i, "Plane");
					}
					if (name == "grass 1" || name == "Sphere 1" || name == "Cube 1") {
						objectInput->SetSelection(i, name);
					}
				}
			}

			if (ImGui::MenuItem("Cinematic Mountains (1000m)"))
			{
				nodeGraph.Clear();
				SceneInputNode* input = new SceneInputNode(nodeGraph);
				PerlinNoiseNode* noise = new PerlinNoiseNode(nodeGraph);
				RiverNode* river = new RiverNode(nodeGraph);
				HydraulicErosionNode* erosion = new HydraulicErosionNode(nodeGraph);
				OutputNode* output = new OutputNode(nodeGraph);

				// Boost parameters for cinematic rocky mountains
				noise->SetRidged(true);
				noise->SetAmplitude(400.0f); // 400m height on 1000m terrain gives realistic massive peaks
				noise->SetFrequency(0.5f); // Spans ~2 massive peaks across the map
				noise->SetOctaves(8);
				noise->SetPersistence(0.45f); // Prevent high octaves from turning the terrain into chaotic static
				
				// Set erosion to deep carving defaults
				erosion->SetSteps(60); 
				erosion->SetRainRate(1.0f);
				erosion->SetKs(0.9f);
				erosion->SetKd(0.02f);
				erosion->SetMaxDelta(1.0f); 
				erosion->SetSmoothPasses(0); 

				// Original River parameters
				river->SetBaseDepth(1.0f);
				river->SetBaseWidth(15.0f);
				river->SetSmoothPasses(8);

				input->editorPos = glm::vec2(50, 150);
				noise->editorPos = glm::vec2(250, 150);
				erosion->editorPos = glm::vec2(450, 150);
				river->editorPos = glm::vec2(650, 150);
				output->editorPos = glm::vec2(850, 150);

				nodeGraph.AddNode(input);
				nodeGraph.AddNode(noise);
				nodeGraph.AddNode(river);
				nodeGraph.AddNode(erosion);
				nodeGraph.AddNode(output);

				nodeGraph.AddLink(input->outputs[0].id, noise->inputs[0].id);
				nodeGraph.AddLink(noise->outputs[0].id, erosion->inputs[0].id);
				nodeGraph.AddLink(erosion->outputs[0].id, river->inputs[0].id);
				nodeGraph.AddLink(river->outputs[0].id, output->inputs[0].id);

				// Auto-assign Plane and set good erosion defaults
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					if (scene.GetObjects()[i]->GetName() == "Plane") {
						input->SetSelection(i, "Plane");
						scene.GetObjects()[i]->GetTransform().SetScale(glm::vec3(1000.0f, 1.0f, 1000.0f)); // 1000x1000 size
						break;
					}
				}
			}

			if (ImGui::MenuItem("Cinematic Mountains V2 (1000m)"))
			{
				nodeGraph.Clear();
				SceneInputNode* input = new SceneInputNode(nodeGraph);
				PerlinNoiseNode* noise = new PerlinNoiseNode(nodeGraph);
				BeautifulErosionNode* erosion = new BeautifulErosionNode(nodeGraph);
				RiverNode* river = new RiverNode(nodeGraph);
				OutputNode* output = new OutputNode(nodeGraph);

				// Boost parameters for cinematic rocky mountains
				noise->SetRidged(true);
				noise->SetAmplitude(400.0f); // 400m height on 1000m terrain gives realistic massive peaks
				noise->SetFrequency(0.5f); // Spans ~2 massive peaks across the map
				noise->SetOctaves(8);
				noise->SetPersistence(0.45f); // Prevent high octaves from turning the terrain into chaotic static

				// Original River parameters
				river->SetBaseDepth(1.0f);
				river->SetBaseWidth(15.0f);
				river->SetSmoothPasses(8);

				input->editorPos = glm::vec2(50, 150);
				noise->editorPos = glm::vec2(250, 150);
				erosion->editorPos = glm::vec2(450, 150);
				river->editorPos = glm::vec2(650, 150);
				output->editorPos = glm::vec2(850, 150);

				nodeGraph.AddNode(input);
				nodeGraph.AddNode(noise);
				nodeGraph.AddNode(erosion);
				nodeGraph.AddNode(river);
				nodeGraph.AddNode(output);

				nodeGraph.AddLink(input->outputs[0].id, noise->inputs[0].id);
				nodeGraph.AddLink(noise->outputs[0].id, erosion->inputs[0].id);
				nodeGraph.AddLink(erosion->outputs[0].id, river->inputs[0].id);
				nodeGraph.AddLink(river->outputs[0].id, output->inputs[0].id);

				// Auto-assign Plane and set good erosion defaults
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					if (scene.GetObjects()[i]->GetName() == "Plane") {
						input->SetSelection(i, "Plane");
						scene.GetObjects()[i]->GetTransform().SetScale(glm::vec3(1000.0f, 1.0f, 1000.0f)); // 1000x1000 size
						break;
					}
				}
			}

			if (ImGui::MenuItem("Procedural City"))
			{
				// Clear the scene
				nodeGraph.Clear();
				// Create nodes
				SceneInputNode* input = new SceneInputNode(nodeGraph);
				CityGridNode* city = new CityGridNode(nodeGraph);
				OutputNode* output = new OutputNode(nodeGraph);
				BuildingGenNode* buildings = new BuildingGenNode(nodeGraph);

				// Layout positions
				input->editorPos = glm::vec2(50, 150);
				city->editorPos = glm::vec2(300, 150);
				output->editorPos = glm::vec2(550, 100);
				buildings->editorPos = glm::vec2(550, 350);

				nodeGraph.AddNode(input);
				nodeGraph.AddNode(city);
				nodeGraph.AddNode(output);
				nodeGraph.AddNode(buildings);

				// Wire: SceneInput -> CityGrid -> Output (roads pin)
				nodeGraph.AddLink(input->outputs[0].id, city->inputs[0].id);
				nodeGraph.AddLink(city->outputs[0].id, output->inputs[0].id);
				// Wire: CityGrid (plots) -> BuildingGen
				nodeGraph.AddLink(city->outputs[1].id, buildings->inputs[0].id);

				// Auto-select City_Ground for the SceneInput
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					if (scene.GetObjects()[i]->GetName() == "City_Ground") {
						input->SetSelection(i, "City_Ground");
						break;
					}
				}

				// Position camera for city overview
				if (camera) {
					camera->SetPositionAndLookAt(glm::vec3(0.0f, 0.0f, 0.0f), 85.0f);
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

void EditorUI::RenderViewport(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint textureID, Camera* camera, const InputHandler* inputHandler)
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winWidth = displaySize.x;
	float winHeight = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();
	ImVec2 pos(windowState.leftWidth, menuHeight);
	ImVec2 size(winWidth - windowState.leftWidth - windowState.rightWidth, (winHeight - menuHeight) * windowState.midHeightRatio);

	if (windowState.maximizedWindowID == 1) { // Scene Maximized
		pos = ImVec2(0, menuHeight);
		size = ImVec2(winWidth, winHeight - menuHeight);
	} else if (windowState.maximizedWindowID != -1) { // Something ELSE is maximized
		return; // Completely skip rendering
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);


	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	bool sceneOpen = ImGui::Begin("Scene", nullptr, windowFlags);
	ImGui::PopStyleVar();

	if (sceneOpen)
	{
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		
		// Render the scene texture (ensuring it fills the panel)
		if (textureID != 0)
		{
			ImGui::Image((ImTextureID)(intptr_t)textureID, viewportPanelSize, ImVec2(0, 1), ImVec2(1, 0));
		}

		// Overlay interaction Capture
		ImGui::SetCursorPos(ImVec2(0, 0));
		ImGui::InvisibleButton("ViewportInteraction", viewportPanelSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

		// Drag and Drop support for viewport
		if (ImGui::BeginDragDropTarget()) {
			const ImGuiPayload* payload = nullptr;
			bool isAsset = false;
			bool isMaterial = false;

			if (payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) isAsset = true;
			else if (payload = ImGui::AcceptDragDropPayload("MATERIAL_PATH")) isMaterial = true;

			if (payload) {
				const char* pathStr = (const char*)payload->Data;
				std::filesystem::path path(pathStr);
				std::string ext = path.extension().string();
				for (auto& c : ext) c = tolower(c);

				if (isAsset && (ext == ".obj" || ext == ".fbx" || ext == ".dae")) {
					// Calculate world position from mouse ray
					ImVec2 mousePos = ImGui::GetMousePos();
					ImVec2 winPos = ImGui::GetWindowPos();
					ImVec2 winSize = ImGui::GetWindowSize();
					glm::vec3 rayDir = scene.GetMouseRay(mousePos.x - winPos.x, mousePos.y - winPos.y, projection, view, winSize.x, winSize.y);
					// Spawn roughly 10 units in front of the camera along the mouse ray
					glm::vec3 spawnPos = cameraPos + rayDir * 10.0f;
					scene.InstantiateModel(path, spawnPos);
				}
				else if (isMaterial || ext == ".mat") {
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

		// Global Shortcut: Delete key handling for the Viewport
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
			{
				if (!scene.GetSelectedObjectIndices().empty()) {
					scene.DeleteSelectedObjects();
				}
				else if (!scene.GetSelectedLightIndices().empty()) {
					scene.DeleteSelectedLights();
				}
			}
			
			// Copy/Paste
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
			{
				scene.CopySelected();
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
			{
				scene.Paste();
			}

			// Undo/Redo
			if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				scene.GetUndoManager().Undo();
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				scene.GetUndoManager().Redo();
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
			{
				scene.GetUndoManager().Redo();
			}
		}

		// Box Selection Overlay
		if (inputHandler && inputHandler->IsBoxSelecting())
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			// Convert viewport-relative coords to absolute screen coords
			glm::vec2 start = inputHandler->GetBoxSelectStart();
			glm::vec2 end = inputHandler->GetBoxSelectEnd();

			ImVec2 p1(viewportPos.x + start.x, viewportPos.y + start.y);
			ImVec2 p2(viewportPos.x + end.x, viewportPos.y + end.y);

			// Semi-transparent purple fill (matches engine theme)
			drawList->AddRectFilled(p1, p2, IM_COL32(120, 60, 180, 40));
			// Solid purple border
			drawList->AddRect(p1, p2, IM_COL32(120, 60, 180, 200), 0.0f, 0, 1.5f);
		}

		// --- Camera Speed Overlay ---
		if (camera) {
			float currentSpeed = camera->getMoveSpeed();
			if (abs(currentSpeed - lastCameraSpeed) > 0.001f) {
				speedOverlayTimer = 0.5f; // Show for 0.5 seconds
				lastCameraSpeed = currentSpeed;
			}

			if (speedOverlayTimer > 0.0f) {
				speedOverlayTimer -= ImGui::GetIO().DeltaTime;
				
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 winPos = ImGui::GetWindowPos();
				ImVec2 winSize = ImGui::GetWindowSize();
				
				// Center of viewport
				ImVec2 center(winPos.x + winSize.x * 0.5f, winPos.y + winSize.y * 0.5f);
				
				char speedText[32];
				sprintf_s(speedText, "Speed: %.1f", currentSpeed);
				
				ImVec2 textSize = ImGui::CalcTextSize(speedText);
				ImVec2 textPos(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f);
				
				// Draw semi-transparent background box
				float pad = 10.0f;
				drawList->AddRectFilled(ImVec2(textPos.x - pad, textPos.y - pad), 
									  ImVec2(textPos.x + textSize.x + pad, textPos.y + textSize.y + pad), 
									  IM_COL32(0, 0, 0, 150), 5.0f);
				
				// Draw text
				drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), speedText);
			}
		}
	}
	ImGui::End();
}

void EditorUI::RenderHierarchy(SceneManager& scene, int winHeight, Camera* camera)
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winHeightParam = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();

	// Hierarchy on top-left
	ImVec2 pos(0, menuHeight);
	ImVec2 size(windowState.leftWidth, (winHeight - menuHeight) * windowState.leftHeightRatio);

	if (windowState.maximizedWindowID == 0) { // Hierarchy Maximized
		size = ImVec2(displaySize.x, winHeightParam - menuHeight);
	} else if (windowState.maximizedWindowID != -1) { // Something ELSE maximized
		return;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	if (!windowState.isHierarchyOpen) return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f)); 
	bool hierarchyOpen = ImGui::Begin("Scene Hierarchy", &windowState.isHierarchyOpen, windowFlags);
	ImGui::PopStyleVar();

	if (hierarchyOpen)
	{
		windowState.CheckMaximize(0);


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

		// Delete/Copy/Paste keys — only when this window is focused
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
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

			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
			{
				scene.CopySelected();
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
			{
				scene.Paste();
			}

			// Undo/Redo
			if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				scene.GetUndoManager().Undo();
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				scene.GetUndoManager().Redo();
			}
			if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
			{
				scene.GetUndoManager().Redo();
			}
		}

		// Right-click context menu
		if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight))
		{
			glm::vec3 spawnPos = camera ? camera->getCameraPosition() + camera->getCameraDirection() * 10.0f : glm::vec3(0.0f);
			if (ImGui::BeginMenu("Create GameObject"))
			{
				if (ImGui::MenuItem("Plane")) scene.CreateGameObject("Plane", spawnPos);
				if (ImGui::MenuItem("Cube")) scene.CreateGameObject("Cube", spawnPos);
				if (ImGui::MenuItem("Sphere")) scene.CreateGameObject("Sphere", spawnPos);
				if (ImGui::MenuItem("Planet")) scene.CreateGameObject("Planet", spawnPos);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Create Light"))
			{
				if (ImGui::MenuItem("Point Light")) scene.CreateLight(LightType::Point, spawnPos);
				if (ImGui::MenuItem("Spot Light")) scene.CreateLight(LightType::Spot, spawnPos);
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		// Drag-drop target (DRY: uses shared handler)
		if (ImGui::BeginDragDropTarget()) {
			glm::vec3 spawnPos = camera ? camera->getCameraPosition() + camera->getCameraDirection() * 10.0f : glm::vec3(0.0f);
			HandleAssetDrop(scene, spawnPos);
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
		glm::vec3 bMin, bMax;
		obj->GetWorldBounds(bMin, bMax);
		
		glm::vec3 center = (bMin + bMax) * 0.5f;
		float size = glm::length(bMax - bMin);
		// Unity-style focus: Ensure camera is far enough to see the whole object, without an arbitrary small cap
		float focusDistance = glm::max(size * 0.65f, 5.0f);
		
		camera->SetPositionAndLookAt(center, focusDistance);
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

void EditorUI::RenderInspector(SceneManager& scene, int winWidth, int winHeight)
{
	auto& objects = scene.GetObjects();
	auto& lights = scene.GetLights();
	int selectedObj = scene.GetSelectedIndex();
	int selectedLight = scene.GetSelectedLightIndex();

	bool showObjectInspector = (selectedObj >= 0 && selectedLight < 0);
	bool showLightInspector = (selectedLight >= 0 && selectedObj < 0);

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winH = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();

	// Inspector on bottom-left
	ImVec2 pos(0, menuHeight + (winHeight - menuHeight) * windowState.leftHeightRatio);
	ImVec2 size(windowState.leftWidth, (winHeight - menuHeight) * (1.0f - windowState.leftHeightRatio));

	if (windowState.maximizedWindowID == 2) { // Inspector Maximized
		pos = ImVec2(0, menuHeight);
		size = ImVec2(displaySize.x, winH - menuHeight);
	} else if (windowState.maximizedWindowID != -1) { // Something ELSE maximized
		return;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	if (!windowState.isInspectorOpen) return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (!showObjectInspector && !showLightInspector) {
		// Even if empty, show a blank Inspector window for docking consistency
		if (ImGui::Begin("Inspector", &windowState.isInspectorOpen, windowFlags)) {
			windowState.CheckMaximize(2);
			ImGui::TextDisabled("Select an object to inspect");
		}
		ImGui::End();
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	bool inspectorOpen = ImGui::Begin("Inspector", &windowState.isInspectorOpen, windowFlags);
	ImGui::PopStyleVar();

	if (inspectorOpen)
	{
		windowState.CheckMaximize(2);


		if (showObjectInspector)
		{
			if (selectedObj >= (int)objects.size()) { ImGui::End(); return; }
			GameObject* selected = objects[selectedObj];
			Transform& transform = selected->GetTransform();
			char nameBuf[128];
			strncpy_s(nameBuf, sizeof(nameBuf), selected->GetName().c_str(), _TRUNCATE);
			
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			{
				std::string oldName = selected->GetName();
				std::string newName = nameBuf;
				if (oldName != newName) {
					selected->SetName(newName);
					scene.GetNodeGraph().NotifyObjectRenamed(oldName, newName);
				}
			}
			ImGui::PopItemWidth();
			ImGui::Separator();

			// --- Transform (collapsible) ---
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// Capture before-state when user starts editing
				if (!inspectorEditingTransform && ImGui::IsItemActive()) {
					// Will be caught below
				}

				// Snapshot before first edit
				bool anyActive = false;
				
				DrawVec3Control("Position", *transform.GetPositionPtr(), 0.0f, 0.1f);
				anyActive |= ImGui::IsItemActive();
				DrawVec3Control("Rotation", *transform.GetRotationPtr(), 0.0f, 1.0f);
				anyActive |= ImGui::IsItemActive();
				DrawVec3Control("Scale", *transform.GetScalePtr(), 1.0f, 0.01f);
				anyActive |= ImGui::IsItemActive();

				if (anyActive && !inspectorEditingTransform) {
					// User just started editing — snapshot the before state
					inspectorEditingTransform = true;
					inspectorTransformBeforePos = transform.GetPosition();
					inspectorTransformBeforeRot = transform.GetRotation();
					inspectorTransformBeforeScale = transform.GetScale();
				}
				else if (!anyActive && inspectorEditingTransform) {
					// User stopped editing — push undo if values changed
					inspectorEditingTransform = false;
					if (inspectorTransformBeforePos != transform.GetPosition() ||
						inspectorTransformBeforeRot != transform.GetRotation() ||
						inspectorTransformBeforeScale != transform.GetScale())
					{
						std::vector<TransformSnapshot> before = {{ selected, inspectorTransformBeforePos, inspectorTransformBeforeRot, inspectorTransformBeforeScale }};
						std::vector<TransformSnapshot> after = {{ selected, transform.GetPosition(), transform.GetRotation(), transform.GetScale() }};
						scene.GetUndoManager().PushAction(std::make_unique<TransformAction>("Inspector Transform", before, after));
					}
				}
			}

			Planet* planet = dynamic_cast<Planet*>(selected);
			
			// --- Planet (collapsible) ---
			if (planet && ImGui::CollapsingHeader("Planet Generator", ImGuiTreeNodeFlags_DefaultOpen))
			{
				PlanetParams p = planet->GetParams();
				bool meshChanged = false;
				bool uniformsChanged = false;


				if (ImGui::SliderInt("Subdivisions", &p.subdivisions, 1, 8)) meshChanged = true;
				if (ImGui::DragInt("Seed", (int*)&p.seed, 1)) { meshChanged = true; uniformsChanged = true; }

				if (meshChanged || uniformsChanged) {
					planet->SetParams(p);
					if (meshChanged) {
						if (!ImGui::IsAnyItemActive()) planet->Generate();
					}
					planet->UpdateUniforms();
				}
			}

				// --- Texture Layers (collapsible) ---
			if (ImGui::CollapsingHeader("Texture Layers", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto& layers = selected->GetTextureLayers();
				float slotSize = 48.0f;
				int removeIdx = -1;

				for (int li = 0; li < (int)layers.size(); li++)
				{
					ImGui::PushID(li + 5000);
					TextureLayer& layer = layers[li];

					ImGui::Separator();
					ImGui::Text("Layer %d", li);
					ImGui::SameLine();
					if (ImGui::SmallButton("X##RemoveLayer")) removeIdx = li;

					// --- Diffuse slot ---
					ImGui::Text("Diffuse");
					ImGui::SameLine(70.0f);
					Texture* layerTex = layer.texture;
					if (layerTex && layerTex->GetTextureID() != 0) {
						ImGui::Image((ImTextureID)(intptr_t)layerTex->GetTextureID(), ImVec2(slotSize, slotSize), ImVec2(0, 1), ImVec2(1, 0));
					} else {
						ImGui::Button("None##D", ImVec2(slotSize, slotSize));
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
							const char* pathStr = (const char*)payload->Data;
							std::filesystem::path path(pathStr);
							std::string ext = path.extension().string();
							for (auto& c : ext) c = tolower(c);
							if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
								Texture* newTex = new Texture(pathStr);
								if (newTex->LoadTextureA()) {
									layer.texture = newTex;
									layer.texturePath = pathStr;
								} else { delete newTex; }
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (layerTex) { ImGui::SameLine(); if (ImGui::SmallButton("X##CD")) { layer.texture = nullptr; layer.texturePath = ""; } }

					// --- Normal map slot ---
					ImGui::Text("Normal");
					ImGui::SameLine(70.0f);
					Texture* layerNorm = layer.normalMap;
					if (layerNorm && layerNorm->GetTextureID() != 0) {
						ImGui::Image((ImTextureID)(intptr_t)layerNorm->GetTextureID(), ImVec2(slotSize, slotSize), ImVec2(0, 1), ImVec2(1, 0));
					} else {
						ImGui::Button("None##N", ImVec2(slotSize, slotSize));
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
							const char* pathStr = (const char*)payload->Data;
							std::filesystem::path path(pathStr);
							std::string ext = path.extension().string();
							for (auto& c : ext) c = tolower(c);
							if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
								Texture* newTex = new Texture(pathStr);
								if (newTex->LoadTextureA()) {
									layer.normalMap = newTex;
									layer.normalMapPath = pathStr;
								} else { delete newTex; }
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (layerNorm) { ImGui::SameLine(); if (ImGui::SmallButton("X##CN")) { layer.normalMap = nullptr; layer.normalMapPath = ""; } }

					// --- Displacement map slot ---
					ImGui::Text("Displace");
					ImGui::SameLine(70.0f);
					Texture* layerDisp = layer.displacementMap;
					if (layerDisp && layerDisp->GetTextureID() != 0) {
						ImGui::Image((ImTextureID)(intptr_t)layerDisp->GetTextureID(), ImVec2(slotSize, slotSize), ImVec2(0, 1), ImVec2(1, 0));
					} else {
						ImGui::Button("None##DP", ImVec2(slotSize, slotSize));
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
							const char* pathStr = (const char*)payload->Data;
							std::filesystem::path path(pathStr);
							std::string ext = path.extension().string();
							for (auto& c : ext) c = tolower(c);
							if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
								Texture* newTex = new Texture(pathStr);
								if (newTex->LoadTextureGrayscale()) {
									layer.displacementMap = newTex;
									layer.displacementMapPath = pathStr;
								} else { delete newTex; }
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (layerDisp) { ImGui::SameLine(); if (ImGui::SmallButton("X##CDP")) { layer.displacementMap = nullptr; layer.displacementMapPath = ""; } }

					// Blend mode dropdown
					const char* blendModes[] = { "Normal", "Height", "Slope", "Height+Slope" };
					int currentMode = (int)layer.blendMode;
					ImGui::PushItemWidth(120.0f);
					if (ImGui::Combo("Blend", &currentMode, blendModes, IM_ARRAYSIZE(blendModes))) {
						layer.blendMode = (LayerBlendMode)currentMode;
					}

					ImGui::DragFloat("Opacity", &layer.opacity, 0.01f, 0.0f, 1.0f);
					ImGui::DragFloat("Tiling", &layer.tiling, 0.1f, 0.01f, 100.0f);
					ImGui::DragFloat("Disp Scale", &layer.displacementScale, 0.01f, 0.0f, 1.0f);

					if (layer.blendMode == LayerBlendMode::Height || layer.blendMode == LayerBlendMode::HeightSlope) {
						ImGui::DragFloat("Height Min", &layer.heightMin, 1.0f, -1000.0f, 10000.0f);
						ImGui::DragFloat("Height Max", &layer.heightMax, 1.0f, -1000.0f, 10000.0f);
					}
					if (layer.blendMode == LayerBlendMode::Slope || layer.blendMode == LayerBlendMode::HeightSlope) {
						ImGui::DragFloat("Slope Min", &layer.slopeMin, 0.01f, 0.0f, 1.0f);
						ImGui::DragFloat("Slope Max", &layer.slopeMax, 0.01f, 0.0f, 1.0f);
					}

					ImGui::Checkbox("Invert", &layer.invert);
					ImGui::PopItemWidth();

					ImGui::PopID();
				}

				if (removeIdx >= 0) {
					selected->RemoveTextureLayer(removeIdx);
				}

				ImGui::Separator();
				if ((int)layers.size() < MAX_TEXTURE_LAYERS) {
					if (ImGui::Button("+ Add Layer")) {
						TextureLayer newLayer;
						selected->AddTextureLayer(newLayer);
					}
				} else {
					ImGui::TextDisabled("Max %d layers", MAX_TEXTURE_LAYERS);
				}
			}

			// --- GPU Tessellation ---
			{
				bool useTess = selected->GetUseTessellation();
				if (ImGui::Checkbox("GPU Tessellation", &useTess)) {
					selected->SetUseTessellation(useTess);
				}
				if (useTess) {
					bool hasDispMap = false;
					for (const auto& layer : selected->GetTextureLayers()) {
						if (layer.displacementMap) { hasDispMap = true; break; }
					}
					if (hasDispMap) {
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Active");
					} else {
						ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Needs displacement map in a layer");
					}
				}
			}

			// Culling Settings removed to enforce Single Responsibility Principle (Global Culling only)

			// --- Material (collapsible, with preview sphere) ---
			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
			{
				Material* mat = selected->GetMaterial();
				
				if (mat) {
					Shader* currentShader = mat->GetShader() ? mat->GetShader() : scene.GetMainShader();
					
					// Shader Selection
					if (ImGui::BeginCombo("Shader", currentShader ? currentShader->GetVertexPath().c_str() : "None")) {
						// Crawl Assets/Shaders
						std::string shaderDir = "Assets/Shaders";
						if (std::filesystem::exists(shaderDir)) {
							for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
								if (entry.path().extension() == ".vert") {
									std::string vPath = entry.path().string();
									// Replace backslashes with forward slashes for consistency
									std::replace(vPath.begin(), vPath.end(), '\\', '/');

									bool isSelected = (currentShader && currentShader->GetVertexPath() == vPath);
									if (ImGui::Selectable(entry.path().filename().string().c_str(), isSelected)) {
										// Match with .frag
										std::string fPath = vPath;
										size_t lastDot = fPath.find_last_of(".");
										if (lastDot != std::string::npos) {
											fPath = fPath.substr(0, lastDot) + ".frag";
										}

										if (vPath == "Assets/Shaders/shader.vert") {
											mat->SetShader(scene.GetMainShader());
										} else {
											Shader* s = new Shader();
											// Assuming .frag exists with same name
											s->CreateFromFiles(vPath.c_str(), fPath.c_str());
											mat->SetShader(s);
										}
									}
									if (isSelected) ImGui::SetItemDefaultFocus();
								}
							}
						}

						ImGui::EndCombo();
					}
					
					// If using default shader, show some legacy controls or hide them if we want pure shader-driven
					// For now, let's always rely on the DiscoverUniforms list below.

					if (currentShader) {
						ImGui::Separator();
						ImGui::Text("Properties");

						for (auto const& [name, prop] : currentShader->GetUniformProperties()) {
							ImGui::PushID(name.c_str());
							ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
							if (prop.type == Shader::UniformType::Float) {
								float val = mat->GetFloat(name);
								if (ImGui::DragFloat(name.c_str(), &val, 0.01f)) {
									mat->SetFloat(name, val);
								}
								// Defer heavy CPU mesh rebuild until the user finishes dragging to prevent lag
								if (ImGui::IsItemDeactivatedAfterEdit() && name == "radius" && planet) {
									planet->Generate();
								}
							}
							else if (prop.type == Shader::UniformType::Int) {
								int val = mat->GetInt(name);
								if (ImGui::DragInt(name.c_str(), &val, 1)) mat->SetInt(name, val);
							}
							else if (prop.type == Shader::UniformType::Vec3) {
								glm::vec3 val = mat->GetVec3(name);
								if (name.find("Color") != std::string::npos || name.find("color") != std::string::npos) {
									if (ImGui::ColorEdit3(name.c_str(), &val.x)) mat->SetVec3(name, val);
								} else {
									if (ImGui::DragFloat3(name.c_str(), &val.x, 0.01f)) mat->SetVec3(name, val);
								}
							}
							else if (prop.type == Shader::UniformType::Vec2) {
								glm::vec2 val = mat->GetVec2(name);
								if (ImGui::DragFloat2(name.c_str(), &val.x, 0.01f)) mat->SetVec2(name, val);
							}
							else if (prop.type == Shader::UniformType::Vec4) {
								glm::vec4 val = mat->GetVec4(name);
								if (name.find("Color") != std::string::npos || name.find("color") != std::string::npos) {
									if (ImGui::ColorEdit4(name.c_str(), &val.x)) mat->SetVec4(name, val);
								} else {
									if (ImGui::DragFloat4(name.c_str(), &val.x, 0.01f)) mat->SetVec4(name, val);
								}
							}
							ImGui::PopItemWidth();
							ImGui::PopID();
						}
					}

					ImGui::Separator();
					// Live preview sphere
					glm::vec3 previewColor = mat->GetColorRGB();

					RenderMaterialPreview(mat->GetFloat("material.specularIntensity"), mat->GetFloat("material.shininess"), previewColor, 
						(!selected->GetTextureLayers().empty()) ? selected->GetTextureLayers()[0].texture : selected->GetTexture(), 
						(!selected->GetTextureLayers().empty()) ? selected->GetTextureLayers()[0].normalMap : selected->GetNormalMap(), 
						mat->GetVec2("material.tiling"), mat->GetVec2("material.offset"));
						
					if (previewTexture) {
						ImGui::Image((ImTextureID)(intptr_t)previewTexture, ImVec2(PREVIEW_SIZE, PREVIEW_SIZE), ImVec2(0, 1), ImVec2(1, 0));
					}
					if (ImGui::Button("X##ClearMaterial")) selected->SetMaterial(nullptr);
				} else {
					ImGui::TextDisabled("No Material");
					if (ImGui::Button("Add Material")) {
						Material* newMat = new Material(scene.GetMainShader());
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
			if (selectedLight >= (int)lights.size()) { ImGui::End(); return; }
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
			
			if (light->GetDirectionPtr()) {
				if (light->GetLightType() == LightType::Directional) {
					float* pitchPtr = light->GetPitchPtr();
					float* yawPtr = light->GetYawPtr();
					
					float oldP = *pitchPtr;
					float oldY = *yawPtr;
					
					DrawVec2Control("Rotation", *pitchPtr, *yawPtr, "P", "Y", 0.0f, 0.5f);

					if (*pitchPtr != oldP || *yawPtr != oldY) {
						light->GetDirectionalLight()->UpdateDirectionFromEuler();
					}
					
					glm::vec3& dir = *light->GetDirectionPtr();
					ImGui::TextDisabled("Direction Vector: %.2f, %.2f, %.2f", dir.x, dir.y, dir.z);
				}
				else {
					DrawVec3Control("Direction", *light->GetDirectionPtr(), 0.0f, 0.01f);
				}
			}
			
			if (light->GetConstantPtr()) {
				ImGui::Separator();
				ImGui::Text("Attenuation");
				ImGui::SliderFloat("Constant", light->GetConstantPtr(), 0.01f, 2.0f);
				ImGui::SliderFloat("Linear", light->GetLinearPtr(), 0.001f, 0.5f);
				ImGui::SliderFloat("Exponent", light->GetExponentPtr(), 0.001f, 0.5f);
				
				if (light->GetSpotEdgePtr()) {
					ImGui::Separator();
					ImGui::SliderFloat("Spot Edge", light->GetSpotEdgePtr(), 0.0f, 90.0f);
				}
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

void EditorUI::RenderGraphicsSettings(SceneManager* sceneManager)
{
	if (!windowState.isGraphicsSettingsOpen || !graphicsSettingsPtr) return;

	ImGui::SetNextWindowSize(ImVec2(320, 380), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Graphics Settings", &windowState.isGraphicsSettingsOpen, ImGuiWindowFlags_NoCollapse))
	{
		if (ImGui::CollapsingHeader("Culling & Distance", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Global Distances (Base)");
			ImGui::DragFloat("LOD 0 -> 1", &graphicsSettingsPtr->lod0Distance, 1.0f, 1.0f, 1000.0f, "%.0f m");
			ImGui::DragFloat("LOD 1 -> 2", &graphicsSettingsPtr->lod1Distance, 1.0f, 1.0f, 2000.0f, "%.0f m");
			ImGui::DragFloat("LOD 2 -> Cull", &graphicsSettingsPtr->lod2Distance, 1.0f, 1.0f, 5000.0f, "%.0f m");
			ImGui::Spacing();
			ImGui::DragFloat("Render Distance", &graphicsSettingsPtr->renderDistance, 10.0f, 10.0f, 20000.0f, "%.0f m");
			ImGui::DragFloat("Shadow Max Dist", &graphicsSettingsPtr->shadowDistance, 1.0f, 1.0f, 5000.0f, "%.0f m");
		}

		if (ImGui::CollapsingHeader("Screen Space Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Enable toggle
			ImGui::Checkbox("Enable SSAO", &graphicsSettingsPtr->ssaoEnabled);
			ImGui::Separator();

			// Disable controls when SSAO is off
			if (!graphicsSettingsPtr->ssaoEnabled) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
				ImGui::BeginDisabled();
			}

			ImGui::Text("Quality");
			ImGui::SliderInt("Kernel Samples", &graphicsSettingsPtr->ssaoKernelSize, 4, 64);
			ImGui::SliderInt("Blur Size", &graphicsSettingsPtr->ssaoBlurSize, 2, 8);

			ImGui::Spacing();
			ImGui::Text("Effect");
			ImGui::SliderFloat("Radius", &graphicsSettingsPtr->ssaoRadius, 0.01f, 5.0f, "%.3f");
			ImGui::SliderFloat("Bias", &graphicsSettingsPtr->ssaoBias, 0.0f, 0.2f, "%.4f");
			ImGui::SliderFloat("Intensity", &graphicsSettingsPtr->ssaoIntensity, 0.1f, 5.0f, "%.2f");

			ImGui::Spacing();
			ImGui::Separator();

			if (ImGui::Button("Reset Defaults##SSAO", ImVec2(-1, 0)))
			{
				graphicsSettingsPtr->ssaoRadius = 0.5f;
				graphicsSettingsPtr->ssaoBias = 0.025f;
				graphicsSettingsPtr->ssaoIntensity = 1.5f;
				graphicsSettingsPtr->ssaoKernelSize = 64;
				graphicsSettingsPtr->ssaoBlurSize = 4;
			}

			if (!graphicsSettingsPtr->ssaoEnabled) {
				ImGui::EndDisabled();
				ImGui::PopStyleVar();
			}
		}

		if (ImGui::CollapsingHeader("Sky & Environment", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enable Volumetric Sky", &graphicsSettingsPtr->volumetricSkyEnabled);
			ImGui::Separator();

			if (!graphicsSettingsPtr->volumetricSkyEnabled) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
				ImGui::BeginDisabled();
			}

			const char* skyTypes[] = { "Atmospheric", "Universe" };
			int currentSkyType = (int)graphicsSettingsPtr->skyboxType;
			if (ImGui::Combo("Skybox Type", &currentSkyType, skyTypes, IM_ARRAYSIZE(skyTypes))) {
				graphicsSettingsPtr->skyboxType = (SkyboxType)currentSkyType;
			}

			ImGui::Separator();

			if (graphicsSettingsPtr->skyboxType == SkyboxType::Atmospheric) {
				ImGui::Text("Procedural Clouds");
				ImGui::Checkbox("Enable Clouds", &graphicsSettingsPtr->cloudsEnabled);
				
				if (!graphicsSettingsPtr->cloudsEnabled) {
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
					ImGui::BeginDisabled();
				}
				
				ImGui::SliderFloat("Cloud Density", &graphicsSettingsPtr->cloudsDensity, 0.1f, 0.9f);
				ImGui::SliderFloat("Cloud Speed", &graphicsSettingsPtr->cloudsSpeed, 0.0f, 0.5f);
				ImGui::SliderFloat("Cloud Sharpness", &graphicsSettingsPtr->cloudsSharpness, 0.05f, 0.5f);

				if (ImGui::Button("Reset Defaults##Clouds", ImVec2(-1, 0))) {
					graphicsSettingsPtr->cloudsDensity = 0.5f;
					graphicsSettingsPtr->cloudsSpeed = 0.05f;
					graphicsSettingsPtr->cloudsSharpness = 0.3f;
				}

				if (!graphicsSettingsPtr->cloudsEnabled) {
					ImGui::EndDisabled();
					ImGui::PopStyleVar();
				}
			}
			else if (graphicsSettingsPtr->skyboxType == SkyboxType::Universe) {
				ImGui::Text("Universe Settings");
				ImGui::SliderFloat("Star Density", &graphicsSettingsPtr->universeStarDensity, 0.1f, 2.0f);
				ImGui::SliderFloat("Star Brightness", &graphicsSettingsPtr->universeStarBrightness, 0.1f, 5.0f);
				ImGui::SliderFloat("Nebula Intensity", &graphicsSettingsPtr->universeNebulaIntensity, 0.0f, 2.0f);
				ImGui::SliderFloat("Universe Speed", &graphicsSettingsPtr->universeSpeed, 0.0f, 0.1f);
				ImGui::ColorEdit3("Nebula Color 1", &graphicsSettingsPtr->universeNebulaColor1.x);
				ImGui::ColorEdit3("Nebula Color 2", &graphicsSettingsPtr->universeNebulaColor2.x);

				if (ImGui::Button("Reset Defaults##Universe", ImVec2(-1, 0))) {
					graphicsSettingsPtr->universeStarDensity = 0.5f;
					graphicsSettingsPtr->universeStarBrightness = 1.0f;
					graphicsSettingsPtr->universeNebulaIntensity = 0.5f;
					graphicsSettingsPtr->universeSpeed = 0.01f;
					graphicsSettingsPtr->universeNebulaColor1 = glm::vec3(0.5f, 0.2f, 0.8f);
					graphicsSettingsPtr->universeNebulaColor2 = glm::vec3(0.1f, 0.5f, 0.9f);
				}
			}

			if (!graphicsSettingsPtr->volumetricSkyEnabled) {
				ImGui::EndDisabled();
				ImGui::PopStyleVar();
			}
		}

		if (ImGui::CollapsingHeader("Screen Space God Rays", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enable God Rays", &graphicsSettingsPtr->godraysEnabled);
			ImGui::Separator();

			if (!graphicsSettingsPtr->godraysEnabled) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
				ImGui::BeginDisabled();
			}

			ImGui::SliderFloat("Decay", &graphicsSettingsPtr->godraysDecay, 0.8f, 1.0f);
			ImGui::SliderFloat("Density", &graphicsSettingsPtr->godraysDensity, 0.1f, 1.0f);

			if (ImGui::Button("Reset Defaults##GodRays", ImVec2(-1, 0))) {
				graphicsSettingsPtr->godraysDecay = 0.95f;
				graphicsSettingsPtr->godraysDensity = 1.0f;
			}

			if (!graphicsSettingsPtr->godraysEnabled) {
				ImGui::EndDisabled();
				ImGui::PopStyleVar();
			}
		}

		if (ImGui::CollapsingHeader("Debug Visualizers", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("LOD Coloring (R=0, G=1, B=2)", &graphicsSettingsPtr->debugLODColoring);
			ImGui::Checkbox("Show Bounding Spheres", &graphicsSettingsPtr->debugShowBounds);
			ImGui::Checkbox("Freeze Culling Frustum", &graphicsSettingsPtr->debugFreezeCulling);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Freezes the frustum used for culling to current camera position, allowing you to fly 'outside' and see what is being culled.");
			ImGui::Checkbox("Show Wireframe", &graphicsSettingsPtr->showWireframe);

			ImGui::Separator();
			ImGui::Text("Occlusion Culling");
			ImGui::Checkbox("Enable Occlusion Culling", &graphicsSettingsPtr->enableOcclusionCulling);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Uses zero-latency Hi-Z depth map to cull objects behind terrain.");
			
			ImGui::Checkbox("Show Hi-Z Map", &graphicsSettingsPtr->debugShowHiZ);
			
			if (graphicsSettingsPtr->debugShowHiZ && sceneManager && sceneManager->GetHiZTexture() > 0)
			{
				// Generate linearized debug visualization (raw depth is all ~1.0 due to perspective compression)
				sceneManager->GenerateHiZDebug(0.1f, 20000.0f);
				
				GLuint debugTex = sceneManager->GetHiZDebugTexture();
				if (debugTex > 0) {
					ImGui::Text("Hi-Z Depth (Linearized, near=white):");
					ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x * (1080.0f / 1920.0f));
					ImGui::Image((void*)(intptr_t)debugTex, size, ImVec2(0, 1), ImVec2(1, 0));
				}
			}
		}
	}
	ImGui::End();
}
