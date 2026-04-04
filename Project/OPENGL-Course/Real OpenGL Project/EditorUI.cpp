#include "EditorUI.h"
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
#include "MergeMeshNode.h"
#include "OutputNode.h"
#include "HydraulicErosionNode.h"
#include "SceneSerializer.h"
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




void EditorUI::Render(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint sceneTextureID, Camera* camera)
{
	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);

	RenderHierarchy(scene, bufferHeight, camera);
	RenderViewport(scene, projection, view, cameraPos, sceneTextureID);
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


void EditorUI::RenderMainMenuBar(SceneManager& scene, NodeGraph& nodeGraph)
{
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

		if (ImGui::BeginMenu("GameObject"))
		{
			if (ImGui::MenuItem("Create Empty")) { scene.CreateGameObject("Empty Object"); }
			ImGui::Separator();
			if (ImGui::MenuItem("3D Object -> Plane")) { scene.CreateGameObject("Plane"); }
			if (ImGui::MenuItem("3D Object -> Cube")) { scene.CreateGameObject("Cube"); }
			if (ImGui::MenuItem("3D Object -> Sphere")) { scene.CreateGameObject("Sphere"); }
			ImGui::Separator();
			if (ImGui::BeginMenu("Light"))
			{
				if (ImGui::MenuItem("Point Light")) { scene.CreateLight(LightType::Point); }
				if (ImGui::MenuItem("Spot Light")) { scene.CreateLight(LightType::Spot); }
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
				PerlinNoiseNode* groundNoise = new PerlinNoiseNode(nodeGraph);
				ScatterNode* scatter = new ScatterNode(nodeGraph);
				OutputNode* groundOutput = new OutputNode(nodeGraph);

				groundInput->editorPos = glm::vec2(50, 50);
				groundNoise->editorPos = glm::vec2(250, 50);
				groundOutput->editorPos = glm::vec2(450, 50);

				rockInput->editorPos = glm::vec2(50, 250);
				scatter->editorPos = glm::vec2(450, 250);

				scatter->SetSpawnAsObjects(true);
				groundOutput->SetSameAsInput(true);

				nodeGraph.AddNode(groundInput);
				nodeGraph.AddNode(rockInput);
				nodeGraph.AddNode(groundNoise);
				nodeGraph.AddNode(scatter);
				nodeGraph.AddNode(groundOutput);

				// Connect Ground Pipeline
				nodeGraph.AddLink(groundInput->outputs[0].id, groundNoise->inputs[0].id);
				nodeGraph.AddLink(groundNoise->outputs[0].id, groundOutput->inputs[0].id);

				// Connect Scatter Surface (from noisy ground)
				nodeGraph.AddLink(groundNoise->outputs[0].id, scatter->inputs[0].id);

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

			if (ImGui::MenuItem("Cinematic Mountains (1000m)"))
			{
				nodeGraph.Clear();
				SceneInputNode* input = new SceneInputNode(nodeGraph);
				PerlinNoiseNode* noise = new PerlinNoiseNode(nodeGraph);
				HydraulicErosionNode* erosion = new HydraulicErosionNode(nodeGraph);
				OutputNode* output = new OutputNode(nodeGraph);

				// Boost parameters for cinematic rocky mountains
				noise->SetRidged(true);
				noise->SetAmplitude(400.0f);
				noise->SetFrequency(0.035f);
				noise->SetOctaves(8);
				
				// Set erosion to deep carving defaults
				erosion->SetSteps(10);
				erosion->SetRainRate(0.1f);
				erosion->SetKs(0.12f);
				erosion->SetKd(0.12f);
				erosion->SetMaxDelta(8.0f); // Allow very sharp ridges

				input->editorPos = glm::vec2(50, 150);
				noise->editorPos = glm::vec2(250, 150);
				erosion->editorPos = glm::vec2(450, 150);
				output->editorPos = glm::vec2(650, 150);

				nodeGraph.AddNode(input);
				nodeGraph.AddNode(noise);
				nodeGraph.AddNode(erosion);
				nodeGraph.AddNode(output);

				nodeGraph.AddLink(input->outputs[0].id, noise->inputs[0].id);
				nodeGraph.AddLink(noise->outputs[0].id, erosion->inputs[0].id);
				nodeGraph.AddLink(erosion->outputs[0].id, output->inputs[0].id);

				// Auto-assign Plane and set good erosion defaults
				for (int i = 0; i < (int)scene.GetObjects().size(); i++) {
					if (scene.GetObjects()[i]->GetName() == "Plane") {
						input->SetSelection(i, "Plane");
						scene.GetObjects()[i]->GetTransform().SetScale(glm::vec3(1000.0f, 1.0f, 1000.0f)); // 1000x1000 size
						break;
					}
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

void EditorUI::RenderViewport(SceneManager& scene, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos, GLuint textureID)
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
					glm::vec3 spawnPos(0.0f);
					if (!scene.RayPlaneIntersect(cameraPos, rayDir, glm::vec3(0,0,0), glm::vec3(0,1,0), spawnPos)) {
						spawnPos = cameraPos + rayDir * 5.0f;
					}
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
		glm::mat4 worldMat = obj->GetWorldMatrix();
		glm::vec3 worldPos = glm::vec3(worldMat[3]);
		
		// Calculate approx max scale from the matrix columns
		float scaleX = glm::length(glm::vec3(worldMat[0]));
		float scaleY = glm::length(glm::vec3(worldMat[1]));
		float scaleZ = glm::length(glm::vec3(worldMat[2]));
		float maxScale = glm::max(scaleX, glm::max(scaleY, scaleZ));
		
		// Ensure a minimum distance of 5.0, but scale up for large objects (e.g. 1000x terrain)
		float focusDistance = glm::max(5.0f, maxScale * 1.5f);
		
		camera->SetPositionAndLookAt(worldPos, focusDistance);
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
				selected->SetName(nameBuf);
			}
			ImGui::PopItemWidth();
			ImGui::Separator();

			// --- Transform (collapsible) ---
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				DrawVec3Control("Position", *transform.GetPositionPtr(), 0.0f, 0.1f);
				DrawVec3Control("Rotation", *transform.GetRotationPtr(), 0.0f, 1.0f);
				DrawVec3Control("Scale", *transform.GetScalePtr(), 1.0f, 0.01f);
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
								if (ImGui::DragFloat(name.c_str(), &val, 0.01f)) mat->SetFloat(name, val);
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
							ImGui::PopItemWidth();
							ImGui::PopID();
						}
					}

					ImGui::Separator();
					// Live preview sphere
					glm::vec3 previewColor = mat->GetColor();

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

