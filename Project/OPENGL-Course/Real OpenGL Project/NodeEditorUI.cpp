#include "NodeEditorUI.h"
#include "NodeGraph.h"
#include "PerlinNoiseNode.h"
#include "SceneInputNode.h"
#include "ScatterNode.h"
#include "OutputNode.h"

#include "imgui.h"
#include <GLFW/glfw3.h>
#include "External Libs/imnodes/imnodes.h"

NodeEditorUI::NodeEditorUI()
	: isOpen(true), showContextMenu(false)
{
}

NodeEditorUI::~NodeEditorUI()
{
}

void NodeEditorUI::Render(NodeGraph& graph, SceneManager& scene, Texture* defaultTex, Material* defaultMat)
{
	if (!isOpen) return;

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);
	ImGui::SetNextWindowPos(ImVec2((float)bufferWidth * 0.3f, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth * 0.7f, (float)bufferHeight * 0.7f), ImGuiCond_FirstUseEver);

	ImGui::Begin("Node Editor", &isOpen, ImGuiWindowFlags_MenuBar);

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

	// Node Editor
	ImNodes::BeginNodeEditor();

	RenderNodes(graph, &scene);
	RenderLinks(graph);

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
				
				// Place node at mouse position
				ImVec2 mousePos = ImGui::GetMousePos();
				newNode->editorPos = glm::vec2(mousePos.x, mousePos.y);
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
		if (!node->positionSet)
		{
			ImNodes::SetNodeScreenSpacePos(node->id, ImVec2(node->editorPos.x, node->editorPos.y));
			node->positionSet = true;
		}

		ImNodes::BeginNode(node->id);

		// Title Bar
		ImNodes::BeginNodeTitleBar();
		ImGui::TextUnformatted(node->title.c_str());
		ImNodes::EndNodeTitleBar();

		// Interior Content (UI parameters)
		ImGui::PushItemWidth(120.0f);
		node->RenderContent(scene);
		ImGui::PopItemWidth();

		ImGui::Spacing();

		// Input and Output Pins
		float nodeWidth = 150.0f;
		
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
			ImGui::Indent(nodeWidth - textWidth - 20.0f);
			ImGui::TextUnformatted(pin.name.c_str());
			ImGui::Unindent(nodeWidth - textWidth - 20.0f);
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
		if (ImGui::MenuItem("Output")) newNode = new OutputNode(graph);

		if (newNode)
		{
			newNode->editorPos = glm::vec2(contextMenuPos.x, contextMenuPos.y); 
			newNode->positionSet = false; // Trigger SetNodeScreenSpacePos in RenderNodes
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
	if (numSelected > 0 && ImGui::IsKeyReleased(ImGuiKey_Delete))
	{
		std::vector<int> selectedNodes(numSelected);
		ImNodes::GetSelectedNodes(selectedNodes.data());
		for (int nodeId : selectedNodes)
		{
			graph.RemoveNode(nodeId);
		}
	}
}
