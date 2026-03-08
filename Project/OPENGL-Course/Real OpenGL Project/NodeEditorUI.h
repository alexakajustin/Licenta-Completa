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
	
	// Editor location and state
	ImVec2 contextMenuPos;
	ImVec2 editorOrigin;

	void RenderNodes(NodeGraph& graph, SceneManager* scene);
	void RenderLinks(NodeGraph& graph);
	void HandleEditorInteractions(NodeGraph& graph);
};
