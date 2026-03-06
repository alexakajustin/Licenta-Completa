#pragma once

#include "imgui.h"
#include "External Libs/imnodes/imnodes.h"
#include <vector>

class NodeGraph;
class SceneManager;
class Texture;
class Material;

// ImGui visual editor for the procedural generation NodeGraph.
class NodeEditorUI
{
public:
	NodeEditorUI();
	~NodeEditorUI();

	void Render(NodeGraph& graph, SceneManager& scene, Texture* defaultTex, Material* defaultMat);

private:
	bool isOpen;
	float zoomLevel = 1.0f;
	
	// Right-click context menu state
	bool showContextMenu;
	ImVec2 contextMenuPos;

	void RenderNodes(NodeGraph& graph, SceneManager* scene);
	void RenderLinks(NodeGraph& graph);
	void HandleEditorInteractions(NodeGraph& graph);
};
