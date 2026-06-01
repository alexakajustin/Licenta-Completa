#pragma once

#include "imgui.h"
#include "External Libs/imnodes/imnodes.h"
#include <vector>

class NodeGraph;
class SceneManager;
class Texture;
class Material;

#include "Editor/EditorUI.h"

enum class NodeEditorAction {
	None,
	ExecutePipeline,
	ExecuteActiveTab
};

// ImGui visual editor for the procedural generation NodeGraph.
class NodeEditorUI
{
public:
	NodeEditorUI();
	~NodeEditorUI();

	NodeEditorAction Render(SceneManager& scene, Texture* defaultTex, Material* defaultMat, EditorUI::WindowState& uiState);

private:
	bool isOpen;
	
	// Editor location and state
	ImVec2 contextMenuPos;
	ImVec2 editorOrigin;

	// Tab rename state
	char renameBuf[128] = "";
	int renameTargetIndex = -1;
	int lastActiveTab = -1;

	void RenderNodes(NodeGraph& graph, SceneManager* scene);
	void RenderLinks(NodeGraph& graph);
	void HandleEditorInteractions(NodeGraph& graph);
};
