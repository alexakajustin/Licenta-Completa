#include "NodeEditorUI.h"
#include "NodeGraph.h"
#include "PerlinNoiseNode.h"
#include "SceneInputNode.h"
#include "ScatterNode.h"
#include "MergeMeshNode.h"
#include "OutputNode.h"

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

void NodeEditorUI::Render(NodeGraph& graph, SceneManager& scene, Texture* defaultTex, Material* defaultMat, bool* p_open, bool forceLayout)
{
	if (p_open && !*p_open) return;
	if (!p_open && !isOpen) return;

	bool* activeOpen = p_open ? p_open : &isOpen;

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);
	
	ImGuiCond layoutCond = forceLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	ImGui::SetNextWindowPos(ImVec2((float)bufferWidth * 0.2f, 19), layoutCond);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth * 0.8f - 300.0f, (float)bufferHeight * 0.7f - 19), layoutCond);

	ImGui::Begin("Node Editor", activeOpen, ImGuiWindowFlags_MenuBar);

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

	// Node Editor

	// Node Editor
	ImNodes::BeginNodeEditor();

	RenderNodes(graph, &scene);
	RenderLinks(graph);

	// Add a dummy item at the very end of the canvas to satisfy ImGui 1.89+ boundary checks.
	// This is the safest way to ensure that any SetCursorPos movement by imnodes is "finalized".
	ImGui::Dummy(ImVec2(0.3f, 0.3f));

	ImNodes::EndNodeEditor();

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
				
				// Place node at mouse position (convert to grid space)
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
		if (ImGui::MenuItem("Scene Input")) newNode = new SceneInputNode(graph);
		if (ImGui::MenuItem("Scatter")) newNode = new ScatterNode(graph);
		if (ImGui::MenuItem("Merge Mesh")) newNode = new MergeMeshNode(graph);
		if (ImGui::MenuItem("Output")) newNode = new OutputNode(graph);

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
