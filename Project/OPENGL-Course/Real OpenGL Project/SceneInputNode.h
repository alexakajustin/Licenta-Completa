#pragma once

#include "NodeGraph.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "PrimitiveGenerator.h"
#include "imgui.h"
#include <string>

// Picks a GameObject from the scene hierarchy and outputs its mesh data.
class SceneInputNode : public GraphNode
{
public:
	SceneInputNode(NodeGraph& graph);

	void RenderContent(SceneManager* scene) override;
	void Execute() override;

	std::string GetSelectedName() const { return selectedName; }
	int GetSelectedIndex() const { return selectedIndex; }

	void SetSelection(int index, const std::string& name)
	{
		selectedIndex = index;
		selectedName = name;
	}

private:
	int selectedIndex = -1;
	std::string selectedName = "(none)";
};
