#pragma once

#include <string>
#include <vector>
#include "Nodes/CustomNodeDef.h"
#include "Editor/EditorUI.h"

class NodeGraph;
class SceneManager;

// =====================================================================
//  NodeBuilderUI — Panel for visually creating/editing custom nodes
// =====================================================================
//
//  This panel lets users:
//  - Name their custom node and set its category
//  - Add/remove input pins (type + name) via +/- buttons
//  - Add/remove operations from a categorized library via +/- buttons
//  - Reorder operations (drag up/down)
//  - Configure each operation's parameters inline
//  - Add/remove output pins (type + name) via +/- buttons
//  - Save node definitions to JSON files
//  - Load saved definitions from disk
//
class NodeBuilderUI
{
public:
	NodeBuilderUI();
	~NodeBuilderUI();

	// Main render function — call once per frame
	void Render(NodeGraph& graph, EditorUI::WindowState& uiState);

	// Load all custom node definitions from the Assets/CustomNodes/ directory
	void LoadSavedDefinitions();

	// Get all loaded definitions (for the node editor context menu)
	static const std::vector<CustomNodeDef>& GetSavedDefinitions();

	// Check if we need to reload
	bool HasDefinitionsLoaded() const { return definitionsLoaded; }

private:
	// The definition currently being edited
	CustomNodeDef currentDef;

	// Buffer for the node name (ImGui input)
	char nameBuffer[128];
	char categoryBuffer[64];

	// Buffer for new pin names
	char newInputPinName[64];
	char newOutputPinName[64];
	int newInputPinType;
	int newOutputPinType;

	// State
	bool definitionsLoaded;
	int selectedOperationCategory;

	// Saved definitions (static so NodeEditorUI can access them)
	static std::vector<CustomNodeDef> savedDefinitions;

	// Helper: render pin type as a readable string
	static const char* PinTypeLabel(PinDataType type);

	// Helper: render the pin type dropdown
	static int PinTypeDropdown(const char* label, int currentType);

	// Helper: render the "Add Operation" popup
	void RenderAddOperationPopup();

	// Helper: save current definition
	void SaveCurrentDefinition();

	// Helper: load a definition for editing
	void LoadDefinitionForEditing(const CustomNodeDef& def);

	// Helper: clear editor to start fresh
	void ClearEditor();
};
