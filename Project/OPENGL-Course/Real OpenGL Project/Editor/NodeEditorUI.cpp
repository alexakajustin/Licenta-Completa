#include "Editor/NodeEditorUI.h"
#include "Nodes/NodeGraph.h"
#include "Procedural/PerlinNoiseNode.h"
#include "Nodes/SceneInputNode.h"
#include "Procedural/HydraulicErosionNode.h"
#include "Procedural/BeautifulErosionNode.h"
#include "Procedural/RiverNode.h"
#include "Procedural/ScatterNode.h"
#include "Nodes/Math/MergeMeshNode.h"
#include "Nodes/OutputNode.h"
#include "Nodes/CustomNode.h"
#include "Editor/NodeBuilderUI.h"
#include "Nodes/Math/ConstantNode.h"
#include "Nodes/Math/MathNode.h"
#include "Nodes/Math/CompareNode.h"
#include "Nodes/Math/BranchNode.h"
#include "Nodes/Math/FilterTransformListNode.h"
#include "Nodes/Math/ForEachNode.h"
#include "Procedural/CityGridNode.h"
#include "Procedural/BuildingGenNode.h"
#include "Procedural/InteriorGenNode.h"
#include "Nodes/Math/ObjectIntersectionFilterNode.h"
#include "Procedural/SolarSystemNode.h"
#include "Scene/SceneManager.h"

#include "imgui.h"
#include <GLFW/glfw3.h>
#include "External Libs/imnodes/imnodes.h"
#include <cstdint>

NodeEditorUI::NodeEditorUI()
	: isOpen(true)
{
}

NodeEditorUI::~NodeEditorUI()
{
}

NodeEditorAction NodeEditorUI::Render(SceneManager& scene, Texture* defaultTex, Material* defaultMat, EditorUI::WindowState& uiState)
{
	if (!uiState.isNodeEditorOpen) return NodeEditorAction::None;

	NodeEditorAction action = NodeEditorAction::None;

	// Get active graph reference
	NodeGraph& graph = scene.GetNodeGraph();

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
		return NodeEditorAction::None;
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
				if (ImGui::MenuItem("Clear Active Tab")) { graph.Clear(); }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Execute"))
			{
				if (ImGui::MenuItem("Execute Entire Pipeline")) { action = NodeEditorAction::ExecutePipeline; }
				if (ImGui::MenuItem("Execute Active Tab Only")) { action = NodeEditorAction::ExecuteActiveTab; }
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// ========== TAB BAR ==========
		auto& tabs = scene.GetGraphTabs();
		int activeTab = scene.GetActiveTabIndex();

		ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.30f, 0.30f, 0.45f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.20f, 0.25f, 0.40f, 1.0f));

		if (ImGui::BeginTabBar("##GraphTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
		{
			int tabToRemove = -1;
			for (int i = 0; i < (int)tabs.size(); i++)
			{
				bool isOpen = true;
				bool showClose = (int)tabs.size() > 1; // Don't show X on the last remaining tab

				ImGuiTabItemFlags flags = 0;
				if (i == activeTab && activeTab != lastActiveTab)
					flags |= ImGuiTabItemFlags_SetSelected;

				bool tabVisible = false;
				std::string tabId = tabs[i].name + "###GraphTab_" + std::to_string(reinterpret_cast<uint64_t>(tabs[i].graph.get()));
				
				if (showClose)
					tabVisible = ImGui::BeginTabItem(tabId.c_str(), &isOpen, flags);
				else
					tabVisible = ImGui::BeginTabItem(tabId.c_str(), nullptr, flags);

				if (tabVisible)
				{
					if (scene.GetActiveTabIndex() != i)
						scene.SetActiveTabIndex(i);
					ImGui::EndTabItem();
				}

				// Double-click to rename
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				{
					renameTargetIndex = i;
					strncpy_s(renameBuf, sizeof(renameBuf), tabs[i].name.c_str(), _TRUNCATE);
					ImGui::OpenPopup("RenameTabPopup");
				}

				if (!isOpen && showClose)
					tabToRemove = i;
			}

			// "+" button to add new tab
			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
			{
				scene.AddGraphTab("New Tab");
				scene.SetActiveTabIndex((int)tabs.size() - 1);
			}

			ImGui::EndTabBar();

			if (tabToRemove >= 0)
				scene.RemoveGraphTab(tabToRemove);
		}

		ImGui::PopStyleColor(3);

		// Rename popup
		if (ImGui::BeginPopup("RenameTabPopup"))
		{
			ImGui::Text("Rename Tab:");
			if (ImGui::InputText("##RenameInput", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				if (renameTargetIndex >= 0 && renameTargetIndex < (int)tabs.size())
					scene.RenameGraphTab(renameTargetIndex, renameBuf);
				renameTargetIndex = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("OK"))
			{
				if (renameTargetIndex >= 0 && renameTargetIndex < (int)tabs.size())
					scene.RenameGraphTab(renameTargetIndex, renameBuf);
				renameTargetIndex = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Toolbar
		if (ImGui::Button("Execute Pipeline"))
		{
			action = NodeEditorAction::ExecutePipeline;
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

	lastActiveTab = scene.GetActiveTabIndex();

	return action;
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
		ImGui::PushID(node->id);
		ImGui::PushItemWidth(120.0f);
		node->RenderContent(scene);
		ImGui::PopItemWidth();
		ImGui::PopID();

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
		if (ImGui::MenuItem("Beautiful Erosion")) newNode = new BeautifulErosionNode(graph);
		if (ImGui::MenuItem("River")) newNode = new RiverNode(graph);
		if (ImGui::MenuItem("Scene Input")) newNode = new SceneInputNode(graph);
		if (ImGui::MenuItem("Scatter")) newNode = new ScatterNode(graph);
		if (ImGui::MenuItem("Merge Mesh")) newNode = new MergeMeshNode(graph);
		if (ImGui::MenuItem("City Grid")) newNode = new CityGridNode(graph);
		if (ImGui::MenuItem("Building Gen")) newNode = new BuildingGenNode(graph);
		if (ImGui::MenuItem("Interior Gen")) newNode = new InteriorGenNode(graph);
		if (ImGui::MenuItem("Output")) newNode = new OutputNode(graph);
		if (ImGui::MenuItem("Solar System")) newNode = new SolarSystemNode(graph);

		ImGui::Separator();

		// Logic Nodes submenu
		if (ImGui::BeginMenu("Logic"))
		{
			if (ImGui::MenuItem("Compare")) newNode = new CompareNode(graph);
			if (ImGui::MenuItem("Branch (If/Else)")) newNode = new BranchNode(graph);
			if (ImGui::MenuItem("Filter Transforms")) newNode = new FilterTransformListNode(graph);
			if (ImGui::MenuItem("Object Filter")) newNode = new ObjectIntersectionFilterNode(graph);
			if (ImGui::MenuItem("For Each")) newNode = new ForEachNode(graph);
			ImGui::EndMenu();
		}

		// Math Nodes submenu
		if (ImGui::BeginMenu("Math"))
		{
			if (ImGui::MenuItem("Math")) newNode = new MathNode(graph);
			ImGui::EndMenu();
		}

		// Constants submenu
		if (ImGui::BeginMenu("Constants"))
		{
			if (ImGui::MenuItem("Float")) newNode = new FloatConstantNode(graph);
			if (ImGui::MenuItem("Int")) newNode = new IntConstantNode(graph);
			if (ImGui::MenuItem("Vec3")) newNode = new Vec3ConstantNode(graph);
			if (ImGui::MenuItem("Bool")) newNode = new BoolConstantNode(graph);
			ImGui::EndMenu();
		}

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
