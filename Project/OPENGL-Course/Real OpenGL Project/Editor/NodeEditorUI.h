#pragma once

#include "imgui.h"
#include <set>
#include <unordered_map>
#include <vector>

class NodeGraph;
class GraphNode;
class SceneManager;
class Texture;
class Material;

#include "Editor/EditorUI.h"

enum class NodeEditorAction {
	None,
	ExecutePipeline,
	ExecuteActiveTab
};

// =====================================================================
// Visual editor of the procedural generation NodeGraph (one canvas per graph tab)
//
// Its own canvas on ImGui draw lists:
//   - zoom toward the cursor (mouse wheel); text and widgets stay crisp at any zoom (ImGui 1.92
//     dynamic fonts); far out the nodes collapse to their title and pins;
//   - pan with the middle or right mouse button, or Space + left drag;
//   - click / Ctrl+click / box selection, dragging every selected node at once;
//   - links dragged out of pins coloured by type; dropped on empty space they open the add-node
//     menu and connect the new node; dropped on an occupied input they replace its link;
//   - searchable add-node menu (right click, Tab or Shift+A), node and link context menus;
//   - Frame (F) and Arrange (lays the graph out in columns by dependency), Delete, Ctrl+A.
// Node positions live in GraphNode::editorPos (saved with the scene); pan, zoom and selection
// per tab, so switching tabs never loses anything. A graph seen for the first time is framed,
// and laid out first if its nodes overlap.
// =====================================================================
class NodeEditorUI
{
public:
	NodeEditorUI();
	~NodeEditorUI();

	NodeEditorAction Render(SceneManager& scene, Texture* defaultTex, Material* defaultMat, EditorUI::WindowState& uiState);

	// Where a pin was drawn last frame (tools and tests)
	bool GetPinScreenPos(int pinId, ImVec2& pos) const
	{
		auto it = pins.find(pinId);
		if (it == pins.end()) return false;
		pos = it->second.pos;
		return true;
	}

private:
	// Measured layout of a node (grid units, relative to its position)
	struct NodeLayout {
		ImVec2 size = ImVec2(220.0f, 120.0f);
		float pinsTop = 60.0f;   // Where the pin rows start
		bool measured = false;
	};

	// View and selection of one graph (tab)
	struct GraphView {
		ImVec2 pan = ImVec2(40.0f, 40.0f); // Screen offset of the grid origin from the canvas corner
		float zoom = 1.0f;
		int framesSeen = 0;                 // First appearance: frame (and untangle) once sizes are known
		std::vector<int> order;             // Node ids, back to front
		std::set<int> selectedNodes;
		std::set<int> selectedLinks;
		std::unordered_map<int, NodeLayout> layouts;
	};

	// One pin as drawn this frame
	struct PinInfo {
		ImVec2 pos;           // Screen centre of the pin
		ImVec2 hitMin, hitMax; // Pin + label: where a drag can start
		int nodeId;
		int type;             // PinType
		bool output;
	};

	enum class Drag { None, Nodes, Pan, Box, Link };

	std::unordered_map<NodeGraph*, GraphView> views;

	// Interaction
	Drag drag = Drag::None;
	int pressNode = -1;         // Node the left button went down on (its widgets keep the mouse)
	bool pressNodeWasSelected = false;
	int panButton = 0;          // Mouse button panning the view (0 = Space + left)
	bool dragMoved = false;
	ImVec2 pressPos;
	int linkPin = -1;           // Pin a link is being dragged from
	bool rightPressedInCanvas = false;
	bool rightDragged = false;
	ImVec2 rightPressPos;

	// Menus
	ImVec2 menuGridPos;
	char searchBuf[64] = "";
	int pendingLinkPin = -1;    // A link dropped on empty space: the new node gets connected to it
	int contextNode = -1;
	int contextLink = -1;
	bool requestFrame = false;
	bool requestArrange = false;
	float requestZoom = 0.0f;   // Toolbar zoom buttons (factor), 0 = none

	// Tab rename
	char renameBuf[128] = "";
	int renameTargetIndex = -1;
	int lastActiveTab = -1;

	// This frame
	ImVec2 canvasMin, canvasMax;
	std::unordered_map<int, PinInfo> pins;
	int lastHoveredPin = -1;
	ImDrawListSplitter splitter; // Per-node draw channels (its own: widgets may split the window's)

	ImVec2 ToScreen(const GraphView& view, const ImVec2& grid) const;
	ImVec2 ToGrid(const GraphView& view, const ImVec2& screen) const;

	void RenderCanvas(NodeGraph& graph, SceneManager& scene, GraphView& view);
	void RenderNode(GraphNode* node, SceneManager& scene, GraphView& view, int drawIndex, int hoveredNode,
		const std::set<int>& connectedPins);
	void RenderLinks(NodeGraph& graph, GraphView& view, int hoveredLink);
	void RenderAddNodeMenu(NodeGraph& graph, GraphView& view);
	void RenderContextMenus(NodeGraph& graph, SceneManager& scene, GraphView& view);

	int HitTestPin(const ImVec2& mouse, int hoveredNode) const;
	int HitTestLink(NodeGraph& graph, const GraphView& view, const ImVec2& mouse) const;
	bool TryLink(NodeGraph& graph, int pinA, int pinB);
	void DeleteSelection(NodeGraph& graph, SceneManager& scene, GraphView& view);
	void FrameNodes(NodeGraph& graph, GraphView& view, bool selectedOnly);
	void ArrangeNodes(NodeGraph& graph, GraphView& view);
	bool NodesOverlap(NodeGraph& graph, GraphView& view) const;
	void SyncView(NodeGraph& graph, GraphView& view);
	void BringToFront(GraphView& view, int nodeId);
};
