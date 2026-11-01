#define IMGUI_DEFINE_MATH_OPERATORS
#include "Editor/NodeEditorUI.h"
#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRegistry.h"
#include "Nodes/SceneInputNode.h"
#include "Nodes/CustomNode.h"
#include "Editor/NodeBuilderUI.h"
#include "Scene/SceneManager.h"

#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace {
	// Pin types of each node type (input types, output types), from throwaway instances on a scratch
	// graph: the add-node menu offers only the nodes a dropped link can connect to
	struct PinTypes { std::set<int> inputs, outputs; };

	const PinTypes& PinTypesOf(const std::string& id, const std::function<GraphNode*(NodeGraph&)>& create)
	{
		static std::map<std::string, PinTypes> cache;
		auto it = cache.find(id);
		if (it != cache.end()) return it->second;
		static NodeGraph scratch;
		PinTypes types;
		if (GraphNode* n = create(scratch)) {
			for (auto& p : n->inputs) types.inputs.insert((int)p.type);
			for (auto& p : n->outputs) types.outputs.insert((int)p.type);
			delete n;
		}
		return cache[id] = types;
	}

	// Whether a node with these pins can take (or feed) a pin of this type
	bool Accepts(const std::set<int>& types, int type, bool intoInputs)
	{
		for (int t : types)
			if (intoInputs ? PinTypesConvertible((PinType)type, (PinType)t) : PinTypesConvertible((PinType)t, (PinType)type)) return true;
		return false;
	}

	ImU32 CategoryColor(const std::string& category)
	{
		if (category == "Input") return IM_COL32(42, 108, 70, 255);
		if (category == "Terrain") return IM_COL32(46, 80, 148, 255);
		if (category == "Points") return IM_COL32(26, 106, 98, 255);
		if (category == "Output") return IM_COL32(138, 46, 66, 255);
		if (category == "Mesh") return IM_COL32(98, 60, 146, 255);
		if (category == "City") return IM_COL32(144, 86, 36, 255);
		if (category == "Space" || category == "World") return IM_COL32(36, 56, 118, 255);
		if (category == "Math") return IM_COL32(110, 92, 42, 255);
		return IM_COL32(90, 54, 134, 255);
	}

	ImU32 HeaderColor(const GraphNode* node)
	{
		return CategoryColor(NodeRegistry::CategoryOf(node));
	}

	ImU32 PinColor(int type, float alpha = 1.0f)
	{
		ImVec4 c;
		switch ((PinType)type) {
		case PinType::Mesh:    c = ImVec4(0.66f, 0.46f, 0.98f, 1.0f); break;
		case PinType::Points:  c = ImVec4(0.98f, 0.62f, 0.26f, 1.0f); break;
		case PinType::Objects: c = ImVec4(0.30f, 0.84f, 0.74f, 1.0f); break;
		case PinType::Float:   c = ImVec4(0.62f, 0.62f, 0.66f, 1.0f); break;
		case PinType::Int:     c = ImVec4(0.36f, 0.76f, 0.96f, 1.0f); break;
		case PinType::Vec3:    c = ImVec4(0.96f, 0.86f, 0.36f, 1.0f); break;
		case PinType::Vec2:    c = ImVec4(0.88f, 0.76f, 0.42f, 1.0f); break;
		case PinType::Bool:    c = ImVec4(0.96f, 0.42f, 0.52f, 1.0f); break;
		default:               c = ImVec4(0.60f, 0.60f, 0.60f, 1.0f); break;
		}
		c.w = alpha;
		return ImGui::ColorConvertFloat4ToU32(c);
	}

	std::string Lower(const char* s)
	{
		std::string r(s);
		for (char& ch : r) ch = (char)std::tolower((unsigned char)ch);
		return r;
	}

	// The link curve between an output (a) and an input (b): leaves and enters horizontally
	void LinkCurve(const ImVec2& a, const ImVec2& b, float zoom, ImVec2& c1, ImVec2& c2)
	{
		float dx = ImMax(std::fabs(b.x - a.x) * 0.5f, 40.0f * zoom);
		c1 = a + ImVec2(dx, 0.0f);
		c2 = b - ImVec2(dx, 0.0f);
	}
}

NodeEditorUI::NodeEditorUI() {}
NodeEditorUI::~NodeEditorUI() {}

ImVec2 NodeEditorUI::ToScreen(const GraphView& view, const ImVec2& grid) const
{
	return canvasMin + view.pan + grid * view.zoom;
}

ImVec2 NodeEditorUI::ToGrid(const GraphView& view, const ImVec2& screen) const
{
	return (screen - canvasMin - view.pan) / view.zoom;
}

// =====================================================================
// Window: menu, graph tabs, toolbar, canvas
// =====================================================================
NodeEditorAction NodeEditorUI::Render(SceneManager& scene, Texture* defaultTex, Material* defaultMat, EditorUI::WindowState& uiState)
{
	(void)defaultTex;
	(void)defaultMat;
	if (!uiState.isNodeEditorOpen) return NodeEditorAction::None;

	NodeEditorAction action = NodeEditorAction::None;

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float menuHeight = ImGui::GetFrameHeight();
	ImVec2 pos(displaySize.x - uiState.rightWidth, menuHeight);
	ImVec2 size(uiState.rightWidth, (displaySize.y - menuHeight) * uiState.rightHeightRatio);
	if (uiState.maximizedWindowID == 4) {
		pos = ImVec2(0, menuHeight);
		size = ImVec2(displaySize.x, displaySize.y - menuHeight);
	} else if (uiState.maximizedWindowID != -1) {
		return NodeEditorAction::None;
	}
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	bool nodeEditorOpen = ImGui::Begin("Node Editor", &uiState.isNodeEditorOpen, windowFlags);
	ImGui::PopStyleVar();

	if (nodeEditorOpen)
	{
		uiState.CheckMaximize(4);

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Clear Active Tab")) {
					// Node by node: what each one generated in the scene goes with it
					NodeGraph& g = scene.GetNodeGraph();
					std::vector<int> ids;
					for (auto* n : g.GetNodes()) ids.push_back(n->id);
					for (int id : ids) g.RemoveNode(id, &scene);
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deletes every node of this tab and what they generated in the scene");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Execute"))
			{
				if (ImGui::MenuItem("Execute Entire Pipeline")) action = NodeEditorAction::ExecutePipeline;
				if (ImGui::MenuItem("Execute Active Tab Only")) action = NodeEditorAction::ExecuteActiveTab;
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem("Frame", "F")) requestFrame = true;
				if (ImGui::MenuItem("Arrange Nodes")) requestArrange = true;
				if (ImGui::MenuItem("Zoom 100%")) requestZoom = -1.0f;
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
				bool isTabOpen = true;
				bool showClose = (int)tabs.size() > 1;

				ImGuiTabItemFlags flags = 0;
				if (i == activeTab && activeTab != lastActiveTab)
					flags |= ImGuiTabItemFlags_SetSelected;

				std::string tabId = tabs[i].name + "###GraphTab_" + std::to_string(reinterpret_cast<uint64_t>(tabs[i].graph.get()));
				bool tabVisible = showClose ? ImGui::BeginTabItem(tabId.c_str(), &isTabOpen, flags)
				                            : ImGui::BeginTabItem(tabId.c_str(), nullptr, flags);
				if (tabVisible)
				{
					if (scene.GetActiveTabIndex() != i) scene.SetActiveTabIndex(i);
					ImGui::EndTabItem();
				}

				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				{
					renameTargetIndex = i;
					strncpy_s(renameBuf, sizeof(renameBuf), tabs[i].name.c_str(), _TRUNCATE);
					ImGui::OpenPopup("RenameTabPopup");
				}

				if (!isTabOpen && showClose) tabToRemove = i;
			}

			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
			{
				scene.AddGraphTab("New Tab");
				scene.SetActiveTabIndex((int)tabs.size() - 1);
			}

			if (tabToRemove >= 0) scene.RemoveGraphTab(tabToRemove);

			if (ImGui::BeginPopup("RenameTabPopup"))
			{
				ImGui::Text("Rename Tab:");
				bool commit = ImGui::InputText("##RenameInput", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				commit = ImGui::Button("OK") || commit;
				if (commit)
				{
					if (renameTargetIndex >= 0 && renameTargetIndex < (int)tabs.size())
						scene.RenameGraphTab(renameTargetIndex, renameBuf);
					renameTargetIndex = -1;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::EndTabBar();
		}
		ImGui::PopStyleColor(3);

		// The graph of the active tab (the tab bar may just have switched it)
		NodeGraph& graph = scene.GetNodeGraph();
		GraphView& view = views[&graph];

		// ========== TOOLBAR ==========
		if (ImGui::Button("Execute Pipeline")) action = NodeEditorAction::ExecutePipeline;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run every tab, in order");
		ImGui::SameLine();
		if (ImGui::Button("Execute Tab")) action = NodeEditorAction::ExecuteActiveTab;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run only this tab's graph");
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ImGui::Button("Frame")) requestFrame = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit the selected nodes, or all of them, in view (F)");
		ImGui::SameLine();
		if (ImGui::Button("Arrange")) requestArrange = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lay the graph out in columns, left to right in the order the data flows");
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ImGui::Button("-##zoomOut")) requestZoom = 1.0f / 1.25f;
		ImGui::SameLine();
		char zoomLabel[32];
		snprintf(zoomLabel, sizeof(zoomLabel), "%d%%##zoomReset", (int)std::lround(view.zoom * 100.0f));
		if (ImGui::Button(zoomLabel, ImVec2(52.0f, 0.0f))) requestZoom = -1.0f;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom (mouse wheel over the canvas). Click: 100%%");
		ImGui::SameLine();
		if (ImGui::Button("+##zoomIn")) requestZoom = 1.25f;

		RenderCanvas(graph, scene, view);

		// Forget the views of tabs that are gone
		std::set<NodeGraph*> live;
		for (auto& t : scene.GetGraphTabs()) live.insert(t.graph.get());
		for (auto it = views.begin(); it != views.end();) {
			if (!live.count(it->first)) it = views.erase(it);
			else ++it;
		}
	}
	ImGui::End();

	lastActiveTab = scene.GetActiveTabIndex();
	return action;
}

// =====================================================================
// Canvas
// =====================================================================
void NodeEditorUI::SyncView(NodeGraph& graph, GraphView& view)
{
	std::set<int> ids;
	for (auto* n : graph.GetNodes()) ids.insert(n->id);
	view.order.erase(std::remove_if(view.order.begin(), view.order.end(), [&](int id) { return !ids.count(id); }), view.order.end());
	for (auto* n : graph.GetNodes())
		if (std::find(view.order.begin(), view.order.end(), n->id) == view.order.end()) view.order.push_back(n->id);
	for (auto it = view.selectedNodes.begin(); it != view.selectedNodes.end();)
		it = ids.count(*it) ? std::next(it) : view.selectedNodes.erase(it);
	std::set<int> linkIds;
	for (auto& l : graph.GetLinks()) linkIds.insert(l.id);
	for (auto it = view.selectedLinks.begin(); it != view.selectedLinks.end();)
		it = linkIds.count(*it) ? std::next(it) : view.selectedLinks.erase(it);
}

void NodeEditorUI::BringToFront(GraphView& view, int nodeId)
{
	auto it = std::find(view.order.begin(), view.order.end(), nodeId);
	if (it != view.order.end() && it + 1 != view.order.end()) {
		view.order.erase(it);
		view.order.push_back(nodeId);
	}
}

void NodeEditorUI::RenderCanvas(NodeGraph& graph, SceneManager& scene, GraphView& view)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(15, 15, 21, 255));
	ImGui::BeginChild("##NodeCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
	ImGui::SetScrollX(0.0f);
	ImGui::SetScrollY(0.0f);

	canvasMin = ImGui::GetWindowPos();
	canvasMax = canvasMin + ImGui::GetWindowSize();
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 mouse = io.MousePos;
	const bool canvasHovered = ImGui::IsWindowHovered();
	const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	const bool ctrl = io.KeyCtrl;

	SyncView(graph, view);
	view.framesSeen++;

	// A graph seen for the first time: once its nodes are measured, untangle them if they
	// overlap (saved positions from before the sizes were known) and frame them
	if (view.framesSeen == 2) {
		if (NodesOverlap(graph, view)) ArrangeNodes(graph, view);
		FrameNodes(graph, view, false);
	}
	if (requestArrange) { ArrangeNodes(graph, view); FrameNodes(graph, view, false); requestArrange = false; }
	if (requestFrame) { FrameNodes(graph, view, !view.selectedNodes.empty()); requestFrame = false; }
	if (requestZoom != 0.0f) {
		ImVec2 center = (canvasMin + canvasMax) * 0.5f;
		ImVec2 g = ToGrid(view, center);
		view.zoom = requestZoom < 0.0f ? 1.0f : ImClamp(view.zoom * requestZoom, 0.2f, 2.5f);
		view.pan = center - canvasMin - g * view.zoom;
		requestZoom = 0.0f;
	}

	// Zoom toward the cursor
	if (canvasHovered && io.MouseWheel != 0.0f) {
		float newZoom = ImClamp(view.zoom * std::pow(1.15f, io.MouseWheel), 0.2f, 2.5f);
		ImVec2 g = ToGrid(view, mouse);
		view.zoom = newZoom;
		view.pan = mouse - canvasMin - g * newZoom;
	}

	// --- Grid: minor lines fade out far away, every 4th line is stronger ---
	{
		float step = 32.0f * view.zoom;
		int minorAlpha = (int)(12.0f * ImClamp((view.zoom - 0.25f) / 0.35f, 0.0f, 1.0f));
		for (int axis = 0; axis < 2; axis++) {
			float origin = axis == 0 ? canvasMin.x + view.pan.x : canvasMin.y + view.pan.y;
			float lo = axis == 0 ? canvasMin.x : canvasMin.y;
			float hi = axis == 0 ? canvasMax.x : canvasMax.y;
			int first = (int)std::floor((lo - origin) / step);
			for (int i = first; origin + i * step < hi; i++) {
				float v = origin + i * step;
				bool major = ((i % 4) + 4) % 4 == 0;
				ImU32 col = major ? IM_COL32(255, 255, 255, 20) : IM_COL32(255, 255, 255, minorAlpha);
				if (!major && minorAlpha == 0) continue;
				if (axis == 0) dl->AddLine(ImVec2(v, canvasMin.y), ImVec2(v, canvasMax.y), col);
				else dl->AddLine(ImVec2(canvasMin.x, v), ImVec2(canvasMax.x, v), col);
			}
		}
	}

	// --- Which node is under the mouse (front to back, last frame's sizes) ---
	int hoveredNode = -1;
	if (canvasHovered) {
		for (int k = (int)view.order.size() - 1; k >= 0; k--) {
			GraphNode* n = graph.FindNode(view.order[k]);
			if (!n) continue;
			ImVec2 p = ToScreen(view, ImVec2(n->editorPos.x, n->editorPos.y));
			ImVec2 q = p + view.layouts[n->id].size * view.zoom;
			if (mouse.x >= p.x && mouse.x < q.x && mouse.y >= p.y && mouse.y < q.y) { hoveredNode = n->id; break; }
		}
	}
	// While the left button is held, the node it went down on keeps the mouse (a slider dragged
	// beyond the node's edge keeps working); pans, box and link drags keep it from every widget
	if (pressNode >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) hoveredNode = pressNode;
	const bool shield = drag == Drag::Pan || drag == Drag::Box || drag == Drag::Link;

	std::set<int> connected;
	for (auto& l : graph.GetLinks()) { connected.insert(l.startPinId); connected.insert(l.endPinId); }

	// --- Nodes: submitted front to back (the frontmost claims the mouse), drawn back to front ---
	pins.clear();
	const int nodeCount = (int)view.order.size();
	splitter.Split(dl, 2 + 2 * nodeCount);
	for (int k = nodeCount - 1; k >= 0; k--) {
		GraphNode* node = graph.FindNode(view.order[k]);
		if (node) RenderNode(node, scene, view, k, shield ? -2 : hoveredNode, connected);
	}

	const bool widgetActive = ImGui::IsAnyItemActive();
	const bool widgetHovered = ImGui::IsAnyItemHovered();
	const int hoveredPin = (canvasHovered && !widgetActive) ? HitTestPin(mouse, hoveredNode) : -1;
	const int hoveredLink = (canvasHovered && hoveredPin < 0 && hoveredNode < 0 && drag == Drag::None) ? HitTestLink(graph, view, mouse) : -1;
	lastHoveredPin = hoveredPin;

	splitter.SetCurrentChannel(dl, 0);
	RenderLinks(graph, view, hoveredLink);

	// --- Overlay: the link being dragged, the selection box, hints ---
	splitter.SetCurrentChannel(dl, 1 + 2 * nodeCount);
	if (drag == Drag::Link && pins.count(linkPin)) {
		const PinInfo& from = pins[linkPin];
		ImVec2 target = mouse;
		bool valid = false;
		if (hoveredPin >= 0 && hoveredPin != linkPin) {
			const PinInfo& c = pins[hoveredPin];
			valid = c.output != from.output && c.nodeId != from.nodeId &&
				(from.output ? PinTypesConvertible((PinType)from.type, (PinType)c.type) : PinTypesConvertible((PinType)c.type, (PinType)from.type));
			if (valid) target = c.pos;
		}
		// Compatible pins light up
		for (auto& [id, p] : pins)
			if (p.output != from.output && p.nodeId != from.nodeId &&
				(from.output ? PinTypesConvertible((PinType)from.type, (PinType)p.type) : PinTypesConvertible((PinType)p.type, (PinType)from.type)))
				dl->AddCircle(p.pos, ImMax(9.0f * view.zoom, 6.0f), PinColor(p.type, 0.55f), 20, 1.5f);
		ImVec2 a = from.output ? from.pos : target, b = from.output ? target : from.pos, c1, c2;
		LinkCurve(a, b, view.zoom, c1, c2);
		dl->AddBezierCubic(a, c1, c2, b, PinColor(from.type, valid || hoveredPin < 0 ? 1.0f : 0.35f), ImMax(2.6f * view.zoom, 1.5f));
	}
	if (drag == Drag::Box) {
		ImVec2 a = ImMin(pressPos, mouse), b = ImMax(pressPos, mouse);
		dl->AddRectFilled(a, b, IM_COL32(150, 110, 235, 28));
		dl->AddRect(a, b, IM_COL32(175, 135, 255, 170));
	}
	{
		const char* hint = graph.GetNodes().empty()
			? "Right click or Tab: add a node  |  drag scene objects here"
			: "Wheel: zoom  |  Middle / right drag: pan  |  Right click: add  |  Drag from a pin: link  |  F: frame  |  Del: delete";
		ImVec2 at(canvasMin.x + 10.0f, canvasMax.y - ImGui::GetFontSize() - 8.0f);
		ImVec2 size = ImGui::CalcTextSize(hint);
		dl->AddRectFilled(at - ImVec2(6.0f, 3.0f), at + size + ImVec2(6.0f, 3.0f), IM_COL32(15, 15, 21, 200), 4.0f);
		dl->AddText(at, IM_COL32(150, 150, 172, 150), hint);
	}
	splitter.Merge(dl);

	// =================================================================
	// Mouse
	// =================================================================
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canvasHovered && drag == Drag::None) {
		pressPos = mouse;
		dragMoved = false;
		if (ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput) {
			drag = Drag::Pan;
			panButton = 0;
		} else if (hoveredPin >= 0) {
			// From an occupied input: pick its link up and re-route it from the output end
			linkPin = hoveredPin;
			// From an occupied single input: pick its link up (a multi input starts a new one)
			Pin* pressed = graph.FindPin(hoveredPin);
			if (!pins[hoveredPin].output && pressed && !pressed->multi) {
				for (auto& l : graph.GetLinks())
					if (l.endPinId == hoveredPin) { linkPin = l.startPinId; graph.RemoveLink(l.id); break; }
			}
			drag = Drag::Link;
		} else if (hoveredNode >= 0) {
			pressNode = hoveredNode;
			BringToFront(view, hoveredNode);
			pressNodeWasSelected = view.selectedNodes.count(hoveredNode) > 0;
			if (ctrl) {
				if (pressNodeWasSelected) view.selectedNodes.erase(hoveredNode);
				else view.selectedNodes.insert(hoveredNode);
			} else if (!pressNodeWasSelected) {
				view.selectedNodes = { hoveredNode };
				view.selectedLinks.clear();
			}
			// On the node's body or title (not on one of its widgets): drag it
			if (!widgetActive && !widgetHovered) drag = Drag::Nodes;
		} else if (hoveredLink >= 0) {
			if (!ctrl) { view.selectedNodes.clear(); view.selectedLinks.clear(); }
			view.selectedLinks.insert(hoveredLink);
		} else {
			drag = Drag::Box;
			if (!ctrl) { view.selectedNodes.clear(); view.selectedLinks.clear(); }
		}
	}
	if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImLengthSqr(mouse - pressPos) > 9.0f) dragMoved = true;

	if (drag == Drag::Nodes && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		ImVec2 d = io.MouseDelta / view.zoom;
		if (d.x != 0.0f || d.y != 0.0f)
			for (int id : view.selectedNodes)
				if (GraphNode* n = graph.FindNode(id)) { n->editorPos.x += d.x; n->editorPos.y += d.y; }
	}
	if (drag == Drag::Pan) view.pan += io.MouseDelta;

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
		if (drag == Drag::Nodes && !dragMoved && !ctrl && pressNodeWasSelected) view.selectedNodes = { pressNode };
		if (drag == Drag::Box) {
			ImVec2 a = ImMin(pressPos, mouse), b = ImMax(pressPos, mouse);
			for (int id : view.order) {
				GraphNode* n = graph.FindNode(id);
				if (!n) continue;
				ImVec2 p = ToScreen(view, ImVec2(n->editorPos.x, n->editorPos.y));
				ImVec2 q = p + view.layouts[id].size * view.zoom;
				if (p.x < b.x && a.x < q.x && p.y < b.y && a.y < q.y) view.selectedNodes.insert(id);
			}
		}
		if (drag == Drag::Link) {
			int target = HitTestPin(mouse, -1);
			if (target >= 0 && target != linkPin) {
				TryLink(graph, linkPin, target);
			} else if (target < 0 && canvasHovered) {
				// Dropped on empty space: add a node there, connected to this pin
				pendingLinkPin = linkPin;
				menuGridPos = ToGrid(view, mouse);
				searchBuf[0] = '\0';
				ImGui::OpenPopup("##AddNode");
			}
			linkPin = -1;
		}
		if (drag != Drag::Pan || panButton == 0) drag = Drag::None;
		pressNode = -1;
	}

	// Middle button: pan
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && canvasHovered && drag == Drag::None) {
		drag = Drag::Pan;
		panButton = 2;
	}
	if (drag == Drag::Pan && panButton == 2 && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) drag = Drag::None;

	// Right button: drag = pan, click = context menu
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && canvasHovered) {
		rightPressedInCanvas = true;
		rightDragged = false;
		rightPressPos = mouse;
	}
	if (rightPressedInCanvas && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !rightDragged && ImLengthSqr(mouse - rightPressPos) > 16.0f) {
		rightDragged = true;
		if (drag == Drag::None) { drag = Drag::Pan; panButton = 1; }
	}
	if (rightPressedInCanvas && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
		if (!rightDragged) {
			if (hoveredNode >= 0) {
				contextNode = hoveredNode;
				if (!view.selectedNodes.count(hoveredNode)) view.selectedNodes = { hoveredNode };
				ImGui::OpenPopup("##NodeMenu");
			} else if (hoveredLink >= 0) {
				contextLink = hoveredLink;
				ImGui::OpenPopup("##LinkMenu");
			} else {
				menuGridPos = ToGrid(view, mouse);
				searchBuf[0] = '\0';
				pendingLinkPin = -1;
				ImGui::OpenPopup("##AddNode");
			}
		}
		rightPressedInCanvas = false;
		if (drag == Drag::Pan && panButton == 1) drag = Drag::None;
	}

	// =================================================================
	// Keyboard (not while typing into a field)
	// =================================================================
	if (canvasFocused && !io.WantTextInput && !widgetActive) {
		if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) DeleteSelection(graph, scene, view);
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameNodes(graph, view, !view.selectedNodes.empty());
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
			view.selectedNodes.clear();
			for (auto* n : graph.GetNodes()) view.selectedNodes.insert(n->id);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			if (drag == Drag::Link) { drag = Drag::None; linkPin = -1; }
			else { view.selectedNodes.clear(); view.selectedLinks.clear(); }
		}
		bool addKey = ImGui::IsKeyPressed(ImGuiKey_Tab, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false));
		if (addKey && canvasHovered && drag == Drag::None) {
			menuGridPos = ToGrid(view, mouse);
			searchBuf[0] = '\0';
			pendingLinkPin = -1;
			ImGui::OpenPopup("##AddNode");
		}
	}

	RenderAddNodeMenu(graph, view);
	RenderContextMenus(graph, scene, view);

	// Scene objects dragged in from the hierarchy become Scene Input nodes
	if (ImGui::BeginDragDropTargetCustom(ImRect(canvasMin, canvasMax), ImGui::GetID("##CanvasDrop"))) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_OBJECT_INDEX")) {
			int objIndex = *(const int*)payload->Data;
			auto& objects = scene.GetObjects();
			if (objIndex >= 0 && objIndex < (int)objects.size()) {
				auto* input = static_cast<SceneInputNode*>(NodeRegistry::Create("scene_object", graph));
				input->SetSelection(objIndex, objects[objIndex]->GetName());
				ImVec2 g = ToGrid(view, mouse);
				input->editorPos = glm::vec2(g.x, g.y);
				graph.AddNode(input);
				view.selectedNodes = { input->id };
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::EndChild();
}

// =====================================================================
// One node: widgets in its foreground channel, then its body behind them
// =====================================================================
void NodeEditorUI::RenderNode(GraphNode* node, SceneManager& scene, GraphView& view, int drawIndex, int hoveredNode,
	const std::set<int>& connectedPins)
{
	ImGuiIO& io = ImGui::GetIO();
	const ImGuiStyle& style = ImGui::GetStyle();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float z = view.zoom;
	NodeLayout& layout = view.layouts[node->id];
	const ImVec2 pos = ToScreen(view, ImVec2(node->editorPos.x, node->editorPos.y));
	const bool selected = view.selectedNodes.count(node->id) > 0;
	// Far out the nodes collapse to their title and pins (tiny widgets help nobody)
	const bool compact = z < 0.42f;
	// Off-screen nodes skip their widgets; every node is measured on its graph's first frame
	ImVec2 approxMax = pos + layout.size * z;
	bool visible = view.framesSeen <= 1 ||
		(approxMax.x > canvasMin.x && pos.x < canvasMax.x && approxMax.y > canvasMin.y && pos.y < canvasMax.y);
	const bool drawWidgets = visible && !compact;

	splitter.SetCurrentChannel(dl, 2 + 2 * drawIndex);
	ImGui::PushFont(nullptr, style.FontSizeBase * z);
	const float fontSize = ImGui::GetFontSize();
	const float pad = 8.0f * z;
	const float titleH = fontSize + 10.0f * z;
	const float rowH = fontSize + 7.0f * z;
	const float pinR = ImMax(4.5f * z, 2.5f);

	// Inline pins are placed by the parameter rows drawn this frame
	for (auto& p : node->inputs) p.anchorY = -1.0f;

	// --- Widgets (only the node under the mouse may react to it) ---
	float contentRight = 0.0f;
	float pinsTop = layout.pinsTop * z;
	if (drawWidgets) {
		ImVec2 savedMouse = io.MousePos;
		bool hideMouse = hoveredNode == -2 || (hoveredNode >= 0 && hoveredNode != node->id);
		if (hideMouse) io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

		ImGui::PushClipRect(pos, pos + layout.size * z, true); // Separators span the window: keep them in
		ImGui::PushID(node->id);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, style.FramePadding * z);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, style.ItemSpacing * z);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, style.ItemInnerSpacing * z);
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, style.IndentSpacing * z);
		ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, style.GrabMinSize * z);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.FrameRounding * z);
		ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, style.GrabRounding * z);
		ImGui::SetCursorScreenPos(pos + ImVec2(pad + 4.0f * z, titleH + pad));
		ImGui::BeginGroup();
		ImGui::PushItemWidth(120.0f * z);
		node->RenderContent(&scene);
		ImGui::PopItemWidth();
		ImGui::EndGroup();
		ImVec2 contentMax = ImGui::GetItemRectMax();
		ImGui::PopStyleVar(7);
		ImGui::PopID();
		ImGui::PopClipRect();
		io.MousePos = savedMouse;

		contentRight = contentMax.x - pos.x;
		pinsTop = (contentMax.y - pos.y) + pad * 0.75f;
	}

	// --- Pin rows: the main inputs (left) and the outputs (right). Parameter pins sit on their
	// rows in the content; linked ones without a row (hidden parameter, compact node) get one here.
	std::vector<Pin*> leftPins;
	for (auto& p : node->inputs)
		if (p.param < 0 || (p.anchorY < 0.0f && connectedPins.count(p.id))) leftPins.push_back(&p);
	const size_t rows = std::max(leftPins.size(), node->outputs.size());

	// Last run: the result (or the error) under the pins
	std::string statusLine;
	ImU32 statusCol = IM_COL32(150, 150, 172, 255);
	if (node->status.failed) {
		statusLine = node->status.message;
		statusCol = IM_COL32(255, 110, 110, 255);
	} else if (node->status.ran) {
		statusLine = node->status.info;
		if (node->status.ms >= 50.0) {
			char buf[48];
			snprintf(buf, sizeof(buf), statusLine.empty() ? "%.0f ms" : "  |  %.0f ms", node->status.ms);
			statusLine += buf;
		}
		if (!node->status.message.empty()) statusCol = IM_COL32(255, 196, 96, 255);
	}
	size_t newline = statusLine.find('\n');
	if (newline != std::string::npos) statusLine = statusLine.substr(0, newline) + " ...";
	const bool showStatus = !compact && !statusLine.empty();

	// --- Size: content, title, pin rows, status ---
	float width = layout.size.x * z;
	float height = layout.size.y * z;
	if (drawWidgets || compact) {
		float rowsW = 0.0f;
		for (size_t r = 0; r < rows; r++) {
			float w = 0.0f;
			if (r < leftPins.size() && !compact) w += ImGui::CalcTextSize(leftPins[r]->name.c_str()).x;
			if (r < node->outputs.size() && !compact) w += ImGui::CalcTextSize(node->outputs[r].name.c_str()).x;
			rowsW = ImMax(rowsW, w + 4.0f * pad);
		}
		float titleW = ImGui::CalcTextSize(node->title.c_str()).x + 3.0f * pad;
		float statusW = showStatus ? ImMin(ImGui::CalcTextSize(statusLine.c_str()).x + 2.0f * pad, 340.0f * z) : 0.0f;
		if (drawWidgets) {
			width = ImMax(ImMax(contentRight + pad, titleW), ImMax(ImMax(rowsW, statusW), 150.0f * z));
			height = pinsTop + rows * rowH + (showStatus ? rowH : 0.0f) + pad * 0.6f;
			layout.size = ImVec2(width, height) / z;
			layout.pinsTop = pinsTop / z;
			layout.measured = true;
		}
	}
	if (compact) pinsTop = titleH + pad * 0.5f;
	const ImVec2 nodeMax = pos + ImVec2(width, height);

	// --- Title ---
	{
		ImU32 textCol = IM_COL32(242, 242, 250, 255);
		if (compact) {
			// Readable when zoomed far out: a larger title centred on the node
			ImGui::PushFont(nullptr, ImMin(style.FontSizeBase * z * 1.8f, style.FontSizeBase));
			ImVec2 ts = ImGui::CalcTextSize(node->title.c_str());
			dl->PushClipRect(pos, nodeMax, true);
			dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f), textCol, node->title.c_str());
			dl->PopClipRect();
			ImGui::PopFont();
		} else {
			dl->PushClipRect(pos, ImVec2(nodeMax.x, pos.y + titleH), true);
			dl->AddText(pos + ImVec2(pad, (titleH - fontSize) * 0.5f), textCol, node->title.c_str());
			dl->PopClipRect();
		}
	}

	// --- Pins ---
	ImU32 labelCol = IM_COL32(205, 205, 218, 255);
	auto drawPin = [&](const ImVec2& c, const Pin& p, bool isConnected, bool isHovered) {
		float r = pinR * (isHovered ? 1.35f : 1.0f);
		// Parameter pins stay small until used
		if (p.param >= 0 && !isConnected && !isHovered) r *= 0.78f;
		ImU32 col = PinColor((int)p.type, p.param >= 0 && !isConnected ? 0.75f : 1.0f);
		float outline = ImMax(1.5f * z, 1.0f);
		if (p.multi) {
			// A multi input (takes any number of links): a rounded bar
			ImVec2 h(r * 0.85f, r * 1.45f);
			dl->AddRectFilled(c - h - ImVec2(outline, outline), c + h + ImVec2(outline, outline), IM_COL32(15, 15, 21, 255), r);
			if (isConnected) dl->AddRectFilled(c - h, c + h, col, r);
			else {
				dl->AddRectFilled(c - h, c + h, IM_COL32(30, 30, 40, 255), r);
				dl->AddRect(c - h, c + h, col, r, 0, ImMax(1.6f * z, 1.0f));
			}
			return;
		}
		dl->AddCircleFilled(c, r + outline, IM_COL32(15, 15, 21, 255));
		if (isConnected) {
			dl->AddCircleFilled(c, r, col);
		} else {
			dl->AddCircleFilled(c, r, IM_COL32(30, 30, 40, 255));
			dl->AddCircle(c, r, col, 0, ImMax(1.6f * z, 1.0f));
		}
	};
	auto registerPin = [&](const Pin& p, const ImVec2& c, const ImVec2& hitMin, const ImVec2& hitMax, bool output) {
		PinInfo info;
		info.pos = c;
		info.hitMin = hitMin;
		info.hitMax = hitMax;
		info.nodeId = node->id;
		info.type = (int)p.type;
		info.output = output;
		pins[p.id] = info;
	};
	const float slop = ImMax(0.0f, 7.0f - pinR); // Small pins are still easy to grab

	// Inline parameter pins, on their rows
	for (auto& p : node->inputs) {
		if (p.param < 0 || p.anchorY < 0.0f) continue;
		ImVec2 c(pos.x, p.anchorY);
		drawPin(c, p, connectedPins.count(p.id) > 0, lastHoveredPin == p.id);
		float reach = ImMax(rowH * 0.5f, pinR + slop);
		registerPin(p, c, ImVec2(c.x - pinR - slop - 2.0f, c.y - reach * 0.8f), ImVec2(c.x + pad, c.y + reach * 0.8f), false);
	}
	for (size_t r = 0; r < rows; r++) {
		float cy = pos.y + pinsTop + (r + 0.5f) * rowH;
		if (r < leftPins.size()) {
			const Pin& p = *leftPins[r];
			ImVec2 c(pos.x, cy);
			float lw = compact ? 0.0f : ImGui::CalcTextSize(p.name.c_str()).x;
			drawPin(c, p, connectedPins.count(p.id) > 0, lastHoveredPin == p.id);
			if (!compact) dl->AddText(ImVec2(pos.x + pad + 2.0f * z, cy - fontSize * 0.5f), labelCol, p.name.c_str());
			registerPin(p, c, ImVec2(c.x - pinR - slop - 2.0f, cy - ImMax(rowH * 0.5f, pinR + slop)),
				ImVec2(pos.x + pad + 2.0f * z + lw + 4.0f, cy + ImMax(rowH * 0.5f, pinR + slop)), false);
		}
		if (r < node->outputs.size()) {
			const Pin& p = node->outputs[r];
			ImVec2 c(pos.x + width, cy);
			float lw = compact ? 0.0f : ImGui::CalcTextSize(p.name.c_str()).x;
			float labelX = pos.x + width - pad - 2.0f * z - lw;
			drawPin(c, p, connectedPins.count(p.id) > 0, lastHoveredPin == p.id);
			if (!compact) dl->AddText(ImVec2(labelX, cy - fontSize * 0.5f), labelCol, p.name.c_str());
			registerPin(p, c, ImVec2(compact ? c.x - pinR - slop - 2.0f : labelX - 4.0f, cy - ImMax(rowH * 0.5f, pinR + slop)),
				ImVec2(c.x + pinR + slop + 2.0f, cy + ImMax(rowH * 0.5f, pinR + slop)), true);
		}
	}

	// --- Status line ---
	if (showStatus) {
		float y = pos.y + pinsTop + rows * rowH + (rowH - fontSize) * 0.5f;
		dl->PushClipRect(pos, ImVec2(nodeMax.x - pad * 0.5f, nodeMax.y), true);
		dl->AddText(ImVec2(pos.x + pad, y), statusCol, statusLine.c_str());
		dl->PopClipRect();
		if (hoveredNode == node->id && (node->status.failed || !node->status.message.empty()) &&
			io.MousePos.y >= y && io.MousePos.y < y + fontSize && !ImGui::IsAnyItemHovered())
			ImGui::SetTooltip("%s", node->status.message.c_str());
	}

	// --- Body behind the widgets ---
	splitter.SetCurrentChannel(dl, 1 + 2 * drawIndex);
	const float rounding = 7.0f * z;
	dl->AddRectFilled(pos + ImVec2(4.0f, 5.0f) * z, nodeMax + ImVec2(4.0f, 5.0f) * z, IM_COL32(0, 0, 0, 70), rounding);
	dl->AddRectFilled(pos, nodeMax, IM_COL32(29, 29, 39, 248), rounding);
	dl->AddRectFilled(pos, ImVec2(nodeMax.x, pos.y + titleH), HeaderColor(node), rounding, compact ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);
	dl->AddLine(ImVec2(pos.x, pos.y + titleH), ImVec2(nodeMax.x, pos.y + titleH), IM_COL32(0, 0, 0, 90));
	bool hovered = hoveredNode == node->id;
	ImU32 border = selected ? IM_COL32(190, 140, 255, 255) : (hovered ? IM_COL32(118, 108, 150, 255) : IM_COL32(58, 58, 74, 255));
	if (node->status.failed && !selected) border = IM_COL32(220, 80, 80, 255);
	dl->AddRect(pos, nodeMax, border, rounding, 0, selected || node->status.failed ? ImMax(2.0f * z, 1.5f) : 1.0f);

	ImGui::PopFont();
}

void NodeEditorUI::RenderLinks(NodeGraph& graph, GraphView& view, int hoveredLink)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float thick = ImMax(2.6f * view.zoom, 1.4f);
	for (auto& l : graph.GetLinks()) {
		auto a = pins.find(l.startPinId), b = pins.find(l.endPinId);
		if (a == pins.end() || b == pins.end()) continue;
		ImVec2 p1 = a->second.pos, p4 = b->second.pos, c1, c2;
		LinkCurve(p1, p4, view.zoom, c1, c2);
		bool sel = view.selectedLinks.count(l.id) > 0;
		bool hot = sel || hoveredLink == l.id || view.selectedNodes.count(a->second.nodeId) || view.selectedNodes.count(b->second.nodeId);
		if (sel) dl->AddBezierCubic(p1, c1, c2, p4, IM_COL32(255, 255, 255, 80), thick + 4.0f);
		dl->AddBezierCubic(p1, c1, c2, p4, PinColor(a->second.type, hot ? 1.0f : 0.72f), hoveredLink == l.id ? thick * 1.6f : thick);
	}
}

// =====================================================================
// Hit tests and edits
// =====================================================================
int NodeEditorUI::HitTestPin(const ImVec2& mouse, int hoveredNode) const
{
	int best = -1;
	float bestDist = FLT_MAX;
	for (auto& [id, p] : pins) {
		// A pin under another node is not reachable through it
		if (hoveredNode >= 0 && p.nodeId != hoveredNode && mouse.x > p.hitMin.x && mouse.x < p.hitMax.x &&
			mouse.y > p.hitMin.y && mouse.y < p.hitMax.y && ImLengthSqr(mouse - p.pos) > 64.0f) continue;
		if (mouse.x < p.hitMin.x || mouse.x > p.hitMax.x || mouse.y < p.hitMin.y || mouse.y > p.hitMax.y) continue;
		float d = ImLengthSqr(mouse - p.pos);
		if (d < bestDist) { bestDist = d; best = id; }
	}
	return best;
}

int NodeEditorUI::HitTestLink(NodeGraph& graph, const GraphView& view, const ImVec2& mouse) const
{
	for (auto& l : graph.GetLinks()) {
		auto a = pins.find(l.startPinId), b = pins.find(l.endPinId);
		if (a == pins.end() || b == pins.end()) continue;
		ImVec2 p1 = a->second.pos, p4 = b->second.pos, c1, c2;
		LinkCurve(p1, p4, view.zoom, c1, c2);
		ImVec2 lo = ImMin(ImMin(p1, p4), ImMin(c1, c2)) - ImVec2(8.0f, 8.0f);
		ImVec2 hi = ImMax(ImMax(p1, p4), ImMax(c1, c2)) + ImVec2(8.0f, 8.0f);
		if (mouse.x < lo.x || mouse.x > hi.x || mouse.y < lo.y || mouse.y > hi.y) continue;
		ImVec2 closest = ImBezierCubicClosestPointCasteljau(p1, c1, c2, p4, mouse, ImGui::GetStyle().CurveTessellationTol);
		if (ImLengthSqr(closest - mouse) < 36.0f) return l.id;
	}
	return -1;
}

bool NodeEditorUI::TryLink(NodeGraph& graph, int pinA, int pinB)
{
	auto a = pins.find(pinA), b = pins.find(pinB);
	if (a == pins.end() || b == pins.end() || a->second.output == b->second.output) return false;
	int out = a->second.output ? pinA : pinB;
	int in = a->second.output ? pinB : pinA;
	// A single input takes one link: the new one replaces it (put back if the new one is refused)
	std::vector<Link> replaced;
	Pin* inPin = graph.FindPin(in);
	if (inPin && !inPin->multi)
		for (auto& l : graph.GetLinks()) if (l.endPinId == in) replaced.push_back(l);
	for (auto& l : replaced) graph.RemoveLink(l.id);
	if (graph.AddLink(out, in)) return true;
	for (auto& l : replaced) graph.GetLinks().push_back(l);
	return false;
}

void NodeEditorUI::DeleteSelection(NodeGraph& graph, SceneManager& scene, GraphView& view)
{
	for (int id : view.selectedLinks) graph.RemoveLink(id);
	for (int id : view.selectedNodes) {
		graph.RemoveNode(id, &scene);
		view.layouts.erase(id);
	}
	view.selectedLinks.clear();
	view.selectedNodes.clear();
	SyncView(graph, view);
}

void NodeEditorUI::FrameNodes(NodeGraph& graph, GraphView& view, bool selectedOnly)
{
	ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
	bool any = false;
	for (auto* n : graph.GetNodes()) {
		if (selectedOnly && !view.selectedNodes.count(n->id)) continue;
		ImVec2 p(n->editorPos.x, n->editorPos.y);
		lo = ImMin(lo, p);
		hi = ImMax(hi, p + view.layouts[n->id].size);
		any = true;
	}
	ImVec2 canvas = canvasMax - canvasMin;
	if (!any || canvas.x < 50.0f || canvas.y < 50.0f) {
		view.zoom = 1.0f;
		view.pan = ImVec2(40.0f, 40.0f);
		return;
	}
	ImVec2 size = hi - lo;
	const float margin = 50.0f;
	float z = ImMin((canvas.x - 2.0f * margin) / ImMax(size.x, 1.0f), (canvas.y - 2.0f * margin) / ImMax(size.y, 1.0f));
	view.zoom = ImClamp(z, 0.3f, 1.0f);
	view.pan = (canvas - size * view.zoom) * 0.5f - lo * view.zoom;
}

// Columns by dependency: a node sits one column right of the deepest node feeding it; within a
// column, nodes are ordered by the height of what feeds them (fewer crossing links)
void NodeEditorUI::ArrangeNodes(NodeGraph& graph, GraphView& view)
{
	auto& nodes = graph.GetNodes();
	if (nodes.empty()) return;
	std::map<int, int> pinOwner, depth;
	for (auto* n : nodes) {
		depth[n->id] = 0;
		for (auto& p : n->inputs) pinOwner[p.id] = n->id;
		for (auto& p : n->outputs) pinOwner[p.id] = n->id;
	}
	for (size_t pass = 0; pass < nodes.size(); pass++) {
		bool changed = false;
		for (auto& l : graph.GetLinks()) {
			auto a = pinOwner.find(l.startPinId), b = pinOwner.find(l.endPinId);
			if (a == pinOwner.end() || b == pinOwner.end()) continue;
			if (depth[b->second] < depth[a->second] + 1) { depth[b->second] = depth[a->second] + 1; changed = true; }
		}
		if (!changed) break;
	}

	std::map<int, std::vector<GraphNode*>> columns;
	for (auto* n : nodes) columns[depth[n->id]].push_back(n);

	const float gapX = 70.0f, gapY = 36.0f;
	std::map<int, float> centerY;
	float x = 0.0f;
	for (auto& [d, column] : columns) {
		auto feedY = [&](GraphNode* n) {
			float sum = 0.0f;
			int count = 0;
			for (auto& l : graph.GetLinks()) {
				auto b = pinOwner.find(l.endPinId);
				auto a = pinOwner.find(l.startPinId);
				if (b == pinOwner.end() || a == pinOwner.end() || b->second != n->id) continue;
				auto c = centerY.find(a->second);
				if (c != centerY.end()) { sum += c->second; count++; }
			}
			return count ? sum / count : n->editorPos.y;
		};
		std::stable_sort(column.begin(), column.end(), [&](GraphNode* a, GraphNode* b) { return feedY(a) < feedY(b); });
		float width = 0.0f, total = 0.0f;
		for (auto* n : column) {
			ImVec2 s = view.layouts[n->id].size;
			width = ImMax(width, s.x);
			total += s.y;
		}
		total += gapY * (float)(column.size() - 1);
		float y = -total * 0.5f;
		for (auto* n : column) {
			ImVec2 s = view.layouts[n->id].size;
			n->editorPos = glm::vec2(x, y);
			centerY[n->id] = y + s.y * 0.5f;
			y += s.y + gapY;
		}
		x += width + gapX;
	}
}

bool NodeEditorUI::NodesOverlap(NodeGraph& graph, GraphView& view) const
{
	auto& nodes = graph.GetNodes();
	for (size_t i = 0; i < nodes.size(); i++) {
		auto li = view.layouts.find(nodes[i]->id);
		ImVec2 ai(nodes[i]->editorPos.x, nodes[i]->editorPos.y);
		ImVec2 si = li != view.layouts.end() ? li->second.size : ImVec2(220.0f, 120.0f);
		for (size_t j = i + 1; j < nodes.size(); j++) {
			auto lj = view.layouts.find(nodes[j]->id);
			ImVec2 aj(nodes[j]->editorPos.x, nodes[j]->editorPos.y);
			ImVec2 sj = lj != view.layouts.end() ? lj->second.size : ImVec2(220.0f, 120.0f);
			float w = ImMin(ai.x + si.x, aj.x + sj.x) - ImMax(ai.x, aj.x);
			float h = ImMin(ai.y + si.y, aj.y + sj.y) - ImMax(ai.y, aj.y);
			if (w > 0.0f && h > 0.0f && w * h > 0.1f * ImMin(si.x * si.y, sj.x * sj.y)) return true;
		}
	}
	return false;
}

// =====================================================================
// Menus
// =====================================================================
void NodeEditorUI::RenderAddNodeMenu(NodeGraph& graph, GraphView& view)
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(230.0f, 0.0f), ImVec2(320.0f, 460.0f));
	if (!ImGui::BeginPopup("##AddNode")) {
		pendingLinkPin = -1;
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		ImGui::CloseCurrentPopup();
		pendingLinkPin = -1;
		ImGui::EndPopup();
		return;
	}
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
	ImGui::SetNextItemWidth(-FLT_MIN);
	bool enter = ImGui::InputTextWithHint("##search", "Search nodes...", searchBuf, sizeof(searchBuf), ImGuiInputTextFlags_EnterReturnsTrue);
	std::string query = Lower(searchBuf);

	// A link dropped here: only the nodes it can connect to (an input of its type after an output,
	// an output of its type before an input)
	auto from = pins.find(pendingLinkPin);
	if (from != pins.end())
		ImGui::TextDisabled(from->second.output ? "Nodes taking this output:" : "Nodes feeding this input:");
	auto compatible = [&](const PinTypes& t) {
		if (from == pins.end()) return true;
		return from->second.output ? Accepts(t.inputs, from->second.type, true) : Accepts(t.outputs, from->second.type, false);
	};
	auto matches = [&](const char* name, const char* category) {
		return query.empty() || Lower(name).find(query) != std::string::npos || Lower(category).find(query) != std::string::npos;
	};

	GraphNode* created = nullptr;
	int matchIndex = 0;
	std::string lastCategory;
	for (const NodeTypeInfo& t : NodeRegistry::Types()) {
		if (!matches(t.name.c_str(), t.category.c_str())) continue;
		if (!compatible(PinTypesOf(t.id, t.create))) continue;
		if (query.empty() && t.category != lastCategory) {
			ImGui::SeparatorText(t.category.c_str());
			lastCategory = t.category;
		}
		if (ImGui::Selectable(t.name.c_str()) || (enter && matchIndex == 0)) created = NodeRegistry::Create(t.id, graph);
		if (ImGui::IsItemHovered() && !t.description.empty()) ImGui::SetTooltip("%s", t.description.c_str());
		matchIndex++;
	}
	const auto& customDefs = NodeBuilderUI::GetSavedDefinitions();
	bool customHeader = false;
	for (const auto& def : customDefs) {
		if (!matches(def.name.c_str(), "Custom")) continue;
		if (!compatible(PinTypesOf("custom:" + def.name, [&def](NodeGraph& g) -> GraphNode* { return new CustomNode(g, def); }))) continue;
		if (query.empty() && !customHeader) { ImGui::SeparatorText("Custom"); customHeader = true; }
		if (ImGui::Selectable(def.name.c_str()) || (enter && matchIndex == 0)) created = new CustomNode(graph, def);
		matchIndex++;
	}
	if (matchIndex == 0) ImGui::TextDisabled("No node matches");

	if (created) {
		ImVec2 at = menuGridPos;
		// Dragged backwards out of an input: the new node goes to its left
		if (from != pins.end() && !from->second.output) at.x -= 240.0f;
		created->editorPos = glm::vec2(at.x, at.y);
		graph.AddNode(created);
		view.order.push_back(created->id);
		view.selectedNodes = { created->id };
		view.selectedLinks.clear();
		// Connect it to the link that was dropped here: a pin of the same type first (main pins
		// before parameter pins), else one it converts to
		if (from != pins.end()) {
			PinType type = (PinType)from->second.type;
			auto best = [&](std::vector<Pin>& list, bool intoInputs) -> Pin* {
				Pin* found = nullptr;
				int foundRank = 99;
				for (auto& p : list) {
					bool ok = intoInputs ? PinTypesConvertible(type, p.type) : PinTypesConvertible(p.type, type);
					if (!ok) continue;
					int rank = (p.type == type ? 0 : 2) + (p.param >= 0 ? 1 : 0);
					if (rank < foundRank) { found = &p; foundRank = rank; }
				}
				return found;
			};
			if (from->second.output) {
				if (Pin* p = best(created->inputs, true)) graph.AddLink(pendingLinkPin, p->id);
			} else if (Pin* p = best(created->outputs, false)) {
				Pin* target = graph.FindPin(pendingLinkPin);
				if (target && !target->multi) graph.RemoveLinkByPinId(pendingLinkPin);
				graph.AddLink(p->id, pendingLinkPin);
			}
		}
		pendingLinkPin = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void NodeEditorUI::RenderContextMenus(NodeGraph& graph, SceneManager& scene, GraphView& view)
{
	if (ImGui::BeginPopup("##NodeMenu")) {
		GraphNode* n = graph.FindNode(contextNode);
		if (!n || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			ImGui::CloseCurrentPopup();
		} else {
			ImGui::TextDisabled("%s", n->title.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Frame", "F")) {
				view.selectedNodes = { contextNode };
				FrameNodes(graph, view, true);
			}
			if (ImGui::MenuItem("Disconnect")) {
				for (auto& p : n->inputs) graph.RemoveLinkByPinId(p.id);
				for (auto& p : n->outputs) graph.RemoveLinkByPinId(p.id);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(view.selectedNodes.size() > 1 ? "Delete selected" : "Delete", "Del")) {
				if (!view.selectedNodes.count(contextNode)) view.selectedNodes = { contextNode };
				DeleteSelection(graph, scene, view);
			}
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("##LinkMenu")) {
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
		if (ImGui::MenuItem("Delete link", "Del")) graph.RemoveLink(contextLink);
		ImGui::EndPopup();
	}
}
