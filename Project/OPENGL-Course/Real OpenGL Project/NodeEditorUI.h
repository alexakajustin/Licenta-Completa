#pragma once

#include "imgui.h"
#include "External Libs/imnodes/imnodes.h"
#include <vector>

class NodeGraph;
class SceneManager;
class Texture;
class Material;

#include "EditorUI.h"

// ImGui visual editor for the procedural generation NodeGraph.
class NodeEditorUI
{
public:
	NodeEditorUI();
	~NodeEditorUI();

	bool Render(NodeGraph& graph, SceneManager& scene, Texture* defaultTex, Material* defaultMat, EditorUI::WindowState& uiState);

private:
	bool isOpen;
	
	// Editor location and state
	ImVec2 contextMenuPos;
	ImVec2 editorOrigin;

	void RenderNodes(NodeGraph& graph, SceneManager* scene);
	void RenderLinks(NodeGraph& graph);
	void HandleEditorInteractions(NodeGraph& graph);
};
