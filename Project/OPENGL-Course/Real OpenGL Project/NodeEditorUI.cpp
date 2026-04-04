#include "NodeEditorUI.h"
#include "NodeGraph.h"
#include "PerlinNoiseNode.h"
#include "SceneInputNode.h"
#include "HydraulicErosionNode.h"
#include "ScatterNode.h"
#include "MergeMeshNode.h"
#include "OutputNode.h"
#include "CustomNode.h"
#include "NodeBuilderUI.h"

#include "imgui.h"
#include <GLFW/glfw3.h>
#include "External Libs/imnodes/imnodes.h"

NodeEditorUI::NodeEditorUI()
	: isOpen(true)
{
}

NodeEditorUI::~NodeEditorUI()
{
}

void NodeEditorUI::Render(NodeGraph& graph, SceneManager& scene, Texture* defaultTex, Material* defaultMat, EditorUI::WindowState& uiState)
{
	if (!uiState.isNodeEditorOpen) return;

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winWidth = displaySize.x;
	float winHeight = displaySize.y;
	float menuHeight = ImGui::GetFrameHeight();

	// Node Editor on top-right
	ImVec2 pos(winWidth - uiState.rightWidth, menuHeight);
	ImVec2 size(uiState.rightWidth, (winHeight - menuHeight) * uiState.rightHeightRatio);

	if (uiState.maximizedWindowID == 4) { // Node Editor Maximized
		pos = ImVec2(0, menuHeight);
		size = ImVec2(winWidth, winHeight - menuHeight);
	} else if (uiState.maximizedWindowID != -1) { // Something ELSE maximized
		return;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
	
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	bool nodeEditorOpen = ImGui::Begin("Node Editor", &uiState.isNodeEditorOpen, windowFlags);
	ImGui::PopStyleVar();

	if (nodeEditorOpen)
	{
		uiState.CheckMaximize(4);

		// Menu Bar
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Clear Graph")) { graph.Clear(); }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Execute"))
			{
				if (ImGui::MenuItem("Execute Graph")) { graph.Execute(scene, defaultTex, defaultMat); }
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// Toolbar
		if (ImGui::Button("Execute Graph"))
		{
			graph.Execute(scene, defaultTex, defaultMat);
		}
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "|  Right-click to add nodes");

		ImGui::Separator();

		editorOrigin = ImGui::GetCursorScreenPos();

		// --- Guard against input bleed from overlapping windows (e.g. Node Builder) ---
		// If another ImGui window is capturing the mouse, freeze ImNodes' view of
		// mouse buttons so it doesn't drag selected nodes while the user drags another panel.
		bool editorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		
		// If the user is actively dragging a parameter slider, we MUST NOT freeze the mouse 
		// even if they drag outside the window bounds, otherwise the slider will drop immediately.
		if (ImGui::IsAnyItemActive()) editorHovered = true;
		
		ImGuiIO& io = ImGui::GetIO();
		
		// Save original mouse button state
		bool savedMouseDown[5];
		bool savedMouseClicked[5];
		if (!editorHovered)
		{
			for (int i = 0; i < 5; i++)
			{
				savedMouseDown[i] = io.MouseDown[i];
				savedMouseClicked[i] = io.MouseClicked[i];
				io.MouseDown[i] = false;
				io.MouseClicked[i] = false;
			}
		}

		// Node Editor rendering
		ImNodes::BeginNodeEditor();

		RenderNodes(graph, &scene);
		RenderLinks(graph);

		ImGui::Dummy(ImVec2(0.3f, 0.3f));

		ImNodes::EndNodeEditor();
		
		// Restore original mouse state
		if (!editorHovered)
		{
			for (int i = 0; i < 5; i++)
			{
				io.MouseDown[i] = savedMouseDown[i];
				io.MouseClicked[i] = savedMouseClicked[i];
			}
		}

		HandleEditorInteractions(graph);

		// Handle Drag and Drop into the editor canvas
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX"))
			{
				int objIndex = *(int*)payload->Data;
				auto& objects = scene.GetObjects();
				if (objIndex >= 0 && objIndex < (int)objects.size())
				{
					SceneInputNode* newNode = new SceneInputNode(graph);
					newNode->SetSelection(objIndex, objects[objIndex]->GetName());
					
					ImVec2 mousePos = ImGui::GetMousePos();
					ImVec2 panning = ImNodes::EditorContextGetPanning();
					ImVec2 gridPos = ImVec2(
						(mousePos.x - editorOrigin.x - panning.x),
						(mousePos.y - editorOrigin.y - panning.y)
					);
					newNode->editorPos = glm::vec2(gridPos.x, gridPos.y);
					newNode->positionSet = false;
					
					graph.AddNode(newNode);
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::End();
}


void NodeEditorUI::RenderNodes(NodeGraph& graph, SceneManager* scene)
{
	for (auto* node : graph.GetNodes())
	{
		// Set position once if not set (using screen space coordinates from when it was added)
		// Set position once if not set
		if (!node->positionSet)
		{
			ImNodes::SetNodeGridSpacePos(node->id, ImVec2(node->editorPos.x, node->editorPos.y));
			node->positionSet = true;
		}

		ImNodes::BeginNode(node->id);

		// Minimalistic title bar
		ImNodes::BeginNodeTitleBar();
		ImGui::TextUnformatted(node->title.c_str());
		ImNodes::EndNodeTitleBar();

		// Interior Content (UI parameters)
		ImGui::PushItemWidth(120.0f);
		node->RenderContent(scene);
		ImGui::PopItemWidth();

		ImGui::Spacing();

		// Input and Output Pins
		const float nodeWidth = 150.0f;
		
		// Inputs
		for (auto& pin : node->inputs)
		{
			ImNodes::BeginInputAttribute(pin.id);
			ImGui::TextUnformatted(pin.name.c_str());
			ImNodes::EndInputAttribute();
		}

		// Outputs
		for (auto& pin : node->outputs)
		{
			ImNodes::BeginOutputAttribute(pin.id);
			float textWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
			// Indent based on a fixed node width to avoid infinite expansion feedback loops
			float indent = nodeWidth - textWidth - 10.0f;
			if (indent > 0.0f) ImGui::Indent(indent);
			ImGui::TextUnformatted(pin.name.c_str());
			if (indent > 0.0f) ImGui::Unindent(indent);
			ImNodes::EndOutputAttribute();
		}

		ImNodes::EndNode();
	}
}

void NodeEditorUI::RenderLinks(NodeGraph& graph)
{
	for (auto& link : graph.GetLinks())
	{
		ImNodes::Link(link.id, link.startPinId, link.endPinId);
	}
}

void NodeEditorUI::HandleEditorInteractions(NodeGraph& graph)
{
	// Context Menu (Right Click)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("AddNodeMenu");
		contextMenuPos = ImGui::GetMousePos();
	}

	if (ImGui::BeginPopup("AddNodeMenu"))
	{
		GraphNode* newNode = nullptr;

		if (ImGui::MenuItem("Perlin Noise")) newNode = new PerlinNoiseNode(graph);
		if (ImGui::MenuItem("Hydraulic Erosion")) newNode = new HydraulicErosionNode(graph);
		if (ImGui::MenuItem("Scene Input")) newNode = new SceneInputNode(graph);
		if (ImGui::MenuItem("Scatter")) newNode = new ScatterNode(graph);
		if (ImGui::MenuItem("Merge Mesh")) newNode = new MergeMeshNode(graph);
		if (ImGui::MenuItem("Output")) newNode = new OutputNode(graph);

		// Custom Nodes from saved definitions
		const auto& customDefs = NodeBuilderUI::GetSavedDefinitions();
		if (!customDefs.empty())
		{
			ImGui::Separator();
			if (ImGui::BeginMenu("Custom Nodes"))
			{
				for (const auto& def : customDefs)
				{
					if (ImGui::MenuItem(def.name.c_str()))
						newNode = new CustomNode(graph, def);
				}
				ImGui::EndMenu();
			}
		}

		if (newNode)
		{
			// Convert menu position to grid space
			ImVec2 panning = ImNodes::EditorContextGetPanning();
			ImVec2 gridPos = ImVec2(
				(contextMenuPos.x - editorOrigin.x - panning.x),
				(contextMenuPos.y - editorOrigin.y - panning.y)
			);
			newNode->editorPos = glm::vec2(gridPos.x, gridPos.y); 
			newNode->positionSet = false; // Trigger SetNodeGridSpacePos in RenderNodes
			graph.AddNode(newNode);
		}

		ImGui::EndPopup();
	}

	// Link Creation
	int startPin, endPin;
	if (ImNodes::IsLinkCreated(&startPin, &endPin))
	{
		graph.AddLink(startPin, endPin);
	}

	// Link Deletion
	int linkId;
	if (ImNodes::IsLinkDestroyed(&linkId))
	{
		graph.RemoveLink(linkId);
	}

	// Link Detaching (Drag from pin into empty space)
	int droppedPinId;
	if (ImNodes::IsLinkDropped(&droppedPinId, false))
	{
		graph.RemoveLinkByPinId(droppedPinId);
	}

	// Node Deletion (Delete key)
	const int numSelected = ImNodes::NumSelectedNodes();
	if (numSelected > 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyReleased(ImGuiKey_Delete))
	{
		std::vector<int> selectedNodes(numSelected);
		ImNodes::GetSelectedNodes(selectedNodes.data());
		for (int nodeId : selectedNodes)
		{
			graph.RemoveNode(nodeId);
		}
	}
}
