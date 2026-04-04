#include "NodeBuilderUI.h"
#include "NodeGraph.h"
#include "OperationRegistry.h"
#include "Operation.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>

// Static definitions storage
std::vector<CustomNodeDef> NodeBuilderUI::savedDefinitions;

static const char* CUSTOM_NODES_DIR = "Assets/CustomNodes";

// =====================================================================
//  Construction
// =====================================================================

NodeBuilderUI::NodeBuilderUI()
	: definitionsLoaded(false), selectedOperationCategory(0),
	  newInputPinType(0), newOutputPinType(0)
{
	memset(nameBuffer, 0, sizeof(nameBuffer));
	strncpy_s(nameBuffer, sizeof(nameBuffer), "My Custom Node", _TRUNCATE);
	memset(categoryBuffer, 0, sizeof(categoryBuffer));
	strncpy_s(categoryBuffer, sizeof(categoryBuffer), "Custom", _TRUNCATE);
	memset(newInputPinName, 0, sizeof(newInputPinName));
	strncpy_s(newInputPinName, sizeof(newInputPinName), "Input", _TRUNCATE);
	memset(newOutputPinName, 0, sizeof(newOutputPinName));
	strncpy_s(newOutputPinName, sizeof(newOutputPinName), "Result", _TRUNCATE);

	ClearEditor();
	LoadSavedDefinitions();
	definitionsLoaded = true;
}

NodeBuilderUI::~NodeBuilderUI()
{
}

// =====================================================================
//  Pin Type Helpers
// =====================================================================

const char* NodeBuilderUI::PinTypeLabel(PinDataType type)
{
	switch (type)
	{
	case PinDataType::Mesh:          return "Mesh";
	case PinDataType::Float:         return "Float";
	case PinDataType::Int:           return "Int";
	case PinDataType::Vec3:          return "Vec3";
	case PinDataType::Vec2:          return "Vec2";
	case PinDataType::Bool:          return "Bool";
	case PinDataType::TransformList: return "TransformList";
	default:                         return "None";
	}
}

static const PinDataType allPinTypes[] = {
	PinDataType::Mesh,
	PinDataType::Float,
	PinDataType::Int,
	PinDataType::Vec3,
	PinDataType::Vec2,
	PinDataType::Bool,
	PinDataType::TransformList
};
static const int numPinTypes = 7;

int NodeBuilderUI::PinTypeDropdown(const char* label, int currentType)
{
	const char* preview = PinTypeLabel((PinDataType)currentType);
	if (ImGui::BeginCombo(label, preview))
	{
		for (int i = 0; i < numPinTypes; i++)
		{
			bool selected = (currentType == (int)allPinTypes[i]);
			if (ImGui::Selectable(PinTypeLabel(allPinTypes[i]), selected))
				currentType = (int)allPinTypes[i];
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return currentType;
}

// =====================================================================
//  ClearEditor — reset to blank state
// =====================================================================

void NodeBuilderUI::ClearEditor()
{
	currentDef = CustomNodeDef();
	currentDef.name = "My Custom Node";
	currentDef.category = "Custom";

	// Default: one Mesh input, one Mesh output
	currentDef.inputDefs.push_back(PinDefinition("Mesh", PinDataType::Mesh));
	currentDef.outputDefs.push_back(PinDefinition("Result", PinDataType::Mesh));

	strncpy_s(nameBuffer, sizeof(nameBuffer), "My Custom Node", _TRUNCATE);
	strncpy_s(categoryBuffer, sizeof(categoryBuffer), "Custom", _TRUNCATE);
}

// =====================================================================
//  LoadDefinitionForEditing — load an existing def into the editor
// =====================================================================

void NodeBuilderUI::LoadDefinitionForEditing(const CustomNodeDef& def)
{
	currentDef = def;
	strncpy_s(nameBuffer, sizeof(nameBuffer), def.name.c_str(), _TRUNCATE);
	strncpy_s(categoryBuffer, sizeof(categoryBuffer), def.category.c_str(), _TRUNCATE);
}

// =====================================================================
//  Main Render
// =====================================================================

void NodeBuilderUI::Render(NodeGraph& graph, EditorUI::WindowState& uiState)
{
	if (!uiState.isNodeBuilderOpen) return;

	if (!uiState.isNodeBuilderOpen) return;

	ImGui::SetNextWindowSize(ImVec2(400, 700), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Node Builder", &uiState.isNodeBuilderOpen))
	{
		ImGui::End();
		return;
	}

	// ===== HEADER =====
	ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Create Custom Node");
	ImGui::Separator();

	ImGui::Text("Name:"); ImGui::SameLine();
	if (ImGui::InputText("##NodeName", nameBuffer, sizeof(nameBuffer)))
		currentDef.name = nameBuffer;

	ImGui::Text("Category:"); ImGui::SameLine();
	if (ImGui::InputText("##NodeCategory", categoryBuffer, sizeof(categoryBuffer)))
		currentDef.category = categoryBuffer;

	ImGui::Spacing();
	ImGui::Separator();

	// ===== INPUTS SECTION =====
	ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "INPUTS");
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
	if (ImGui::Button("+ Add Input"))
	{
		std::string pinName = std::string(newInputPinName);
		if (pinName.empty()) pinName = "Input_" + std::to_string(currentDef.inputDefs.size());
		currentDef.inputDefs.push_back(PinDefinition(pinName, (PinDataType)newInputPinType));
	}

	// Pin type for next add
	ImGui::SameLine();
	ImGui::PushItemWidth(80);
	newInputPinType = PinTypeDropdown("##NewInputType", newInputPinType);
	ImGui::PopItemWidth();

	int removeInputIdx = -1;
	for (int i = 0; i < (int)currentDef.inputDefs.size(); i++)
	{
		ImGui::PushID(i + 10000);
		PinDefinition& pin = currentDef.inputDefs[i];

		ImGui::BulletText(""); ImGui::SameLine();

		// Editable name
		char pinNameBuf[64];
		strncpy_s(pinNameBuf, sizeof(pinNameBuf), pin.name.c_str(), _TRUNCATE);
		pinNameBuf[sizeof(pinNameBuf) - 1] = '\0';
		ImGui::PushItemWidth(100);
		if (ImGui::InputText("##PinName", pinNameBuf, sizeof(pinNameBuf)))
			pin.name = pinNameBuf;
		ImGui::PopItemWidth();
		ImGui::SameLine();

		// Type dropdown
		ImGui::PushItemWidth(80);
		int type = (int)pin.type;
		type = PinTypeDropdown("##PinType", type);
		pin.type = (PinDataType)type;
		ImGui::PopItemWidth();
		ImGui::SameLine();

		// Remove button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
		if (ImGui::Button("-##RemoveInput")) removeInputIdx = i;
		ImGui::PopStyleColor();

		ImGui::PopID();
	}
	if (removeInputIdx >= 0)
		currentDef.inputDefs.erase(currentDef.inputDefs.begin() + removeInputIdx);

	ImGui::Spacing();
	ImGui::Separator();

	// ===== OPERATIONS SECTION =====
	ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "OPERATIONS");
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130.0f);
	if (ImGui::Button("+ Add Operation"))
		ImGui::OpenPopup("AddOpPopup");

	RenderAddOperationPopup();

	int removeOpIdx = -1;
	int moveUpIdx = -1;
	int moveDownIdx = -1;

	for (int i = 0; i < (int)currentDef.operations.size(); i++)
	{
		ImGui::PushID(i + 20000);
		OperationSlot& slot = currentDef.operations[i];

		// Operation header with controls
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.35f, 0.5f, 1.0f));

		bool open = ImGui::TreeNodeEx(("##Op" + std::to_string(i)).c_str(),
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);

		ImGui::PopStyleColor(2);

		// Inline index + name on same line as tree node
		ImGui::SameLine(30.0f);
		ImGui::Text("%d. %s", i + 1, slot.operationName.c_str());

		// Move/remove buttons (right-aligned)
		float buttonsStart = ImGui::GetContentRegionAvail().x - 60.0f;
		ImGui::SameLine(buttonsStart + 40.0f);

		if (i > 0) { if (ImGui::ArrowButton("##Up", ImGuiDir_Up)) moveUpIdx = i; }
		else ImGui::Dummy(ImVec2(20, 0));
		ImGui::SameLine();

		if (i < (int)currentDef.operations.size() - 1) { if (ImGui::ArrowButton("##Down", ImGuiDir_Down)) moveDownIdx = i; }
		else ImGui::Dummy(ImVec2(20, 0));
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
		if (ImGui::Button("-##RemoveOp")) removeOpIdx = i;
		ImGui::PopStyleColor();

		if (open)
		{
			// Render the operation's parameters
			Operation* tempOp = OperationRegistry::Get().Create(slot.operationName);
			if (tempOp)
			{
				// Apply current overrides
				for (auto& pair : slot.paramOverrides)
					tempOp->GetParams()[pair.first] = pair.second;

				ImGui::PushItemWidth(120);
				tempOp->RenderUI();
				ImGui::PopItemWidth();

				// Update overrides from the rendered UI
				slot.paramOverrides = tempOp->GetParams();

				delete tempOp;
			}
			else
			{
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "Unknown operation: %s", slot.operationName.c_str());
			}

			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	// Process removal/reorder
	if (removeOpIdx >= 0)
		currentDef.operations.erase(currentDef.operations.begin() + removeOpIdx);
	if (moveUpIdx > 0)
		std::swap(currentDef.operations[moveUpIdx], currentDef.operations[moveUpIdx - 1]);
	if (moveDownIdx >= 0 && moveDownIdx < (int)currentDef.operations.size() - 1)
		std::swap(currentDef.operations[moveDownIdx], currentDef.operations[moveDownIdx + 1]);

	ImGui::Spacing();
	ImGui::Separator();

	// ===== OUTPUTS SECTION =====
	ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "OUTPUTS");
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
	if (ImGui::Button("+ Add Output"))
	{
		std::string pinName = std::string(newOutputPinName);
		if (pinName.empty()) pinName = "Output_" + std::to_string(currentDef.outputDefs.size());
		currentDef.outputDefs.push_back(PinDefinition(pinName, (PinDataType)newOutputPinType));
	}

	ImGui::SameLine();
	ImGui::PushItemWidth(80);
	newOutputPinType = PinTypeDropdown("##NewOutputType", newOutputPinType);
	ImGui::PopItemWidth();

	int removeOutputIdx = -1;
	for (int i = 0; i < (int)currentDef.outputDefs.size(); i++)
	{
		ImGui::PushID(i + 30000);
		PinDefinition& pin = currentDef.outputDefs[i];

		ImGui::BulletText(""); ImGui::SameLine();

		char pinNameBuf[64];
		strncpy_s(pinNameBuf, sizeof(pinNameBuf), pin.name.c_str(), _TRUNCATE);
		pinNameBuf[sizeof(pinNameBuf) - 1] = '\0';
		ImGui::PushItemWidth(100);
		if (ImGui::InputText("##OutPinName", pinNameBuf, sizeof(pinNameBuf)))
			pin.name = pinNameBuf;
		ImGui::PopItemWidth();
		ImGui::SameLine();

		ImGui::PushItemWidth(80);
		int type = (int)pin.type;
		type = PinTypeDropdown("##OutPinType", type);
		pin.type = (PinDataType)type;
		ImGui::PopItemWidth();
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
		if (ImGui::Button("-##RemoveOutput")) removeOutputIdx = i;
		ImGui::PopStyleColor();

		ImGui::PopID();
	}
	if (removeOutputIdx >= 0)
		currentDef.outputDefs.erase(currentDef.outputDefs.begin() + removeOutputIdx);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// ===== VALIDATION =====
	std::string validationError = currentDef.Validate();
	if (!validationError.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "! %s", validationError.c_str());
	}

	// ===== SAVE / LOAD / CLEAR =====
	bool canSave = validationError.empty();

	if (!canSave) ImGui::BeginDisabled();
	if (ImGui::Button("Save Node", ImVec2(120, 30)))
		SaveCurrentDefinition();
	if (!canSave) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("New / Clear", ImVec2(120, 30)))
		ClearEditor();

	ImGui::Spacing();

	// ===== SAVED NODES LIST =====
	if (!savedDefinitions.empty())
	{
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Saved Custom Nodes (%d):", (int)savedDefinitions.size());

		int deleteIdx = -1;
		for (int i = 0; i < (int)savedDefinitions.size(); i++)
		{
			ImGui::PushID(i + 40000);
			const CustomNodeDef& def = savedDefinitions[i];

			ImGui::BulletText("%s", def.name.c_str());
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", def.category.c_str());
			ImGui::SameLine();

			if (ImGui::SmallButton("Edit"))
				LoadDefinitionForEditing(def);

			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
			if (ImGui::SmallButton("-"))
				deleteIdx = i;
			ImGui::PopStyleColor();

			ImGui::PopID();
		}

		// Delete after iteration to avoid invalidating the loop
		if (deleteIdx >= 0)
		{
			const CustomNodeDef& def = savedDefinitions[deleteIdx];
			std::string path = def.filePath;

			std::error_code ec;
			if (std::filesystem::remove(path, ec))
				printf("[NodeBuilder] Deleted '%s' (%s)\n", def.name.c_str(), path.c_str());
			else
				printf("[NodeBuilder] Failed to delete '%s': %s\n", path.c_str(), ec.message().c_str());

			LoadSavedDefinitions();
		}
	}

	ImGui::End();
}

// =====================================================================
//  Add Operation Popup — categorized dropdown
// =====================================================================

void NodeBuilderUI::RenderAddOperationPopup()
{
	if (ImGui::BeginPopup("AddOpPopup"))
	{
		auto categories = OperationRegistry::Get().GetCategories();

		for (const auto& cat : categories)
		{
			if (ImGui::BeginMenu(cat.c_str()))
			{
				auto ops = OperationRegistry::Get().GetNamesByCategory(cat);
				for (const auto& opName : ops)
				{
					if (ImGui::MenuItem(opName.c_str()))
					{
						OperationSlot slot;
						slot.operationName = opName;

						// Initialize with default params
						Operation* tempOp = OperationRegistry::Get().Create(opName);
						if (tempOp)
						{
							slot.paramOverrides = tempOp->GetParams();
							delete tempOp;
						}

						currentDef.operations.push_back(slot);
					}
				}
				ImGui::EndMenu();
			}
		}

		ImGui::EndPopup();
	}
}

// =====================================================================
//  SaveCurrentDefinition
// =====================================================================

void NodeBuilderUI::SaveCurrentDefinition()
{
	currentDef.name = nameBuffer;
	currentDef.category = categoryBuffer;

	// Build filename from node name (sanitize)
	std::string filename = currentDef.name;
	for (char& c : filename)
	{
		if (c == ' ') c = '_';
		else if (!isalnum(c) && c != '_' && c != '-') c = '_';
	}

	std::string path = std::string(CUSTOM_NODES_DIR) + "/" + filename + ".json";

	if (currentDef.SaveToFile(path))
	{
		printf("[NodeBuilder] Saved '%s' to %s\n", currentDef.name.c_str(), path.c_str());
		// Reload to pick up the new/updated definition
		LoadSavedDefinitions();
	}
}

// =====================================================================
//  LoadSavedDefinitions — scan Assets/CustomNodes/ for JSON files
// =====================================================================

void NodeBuilderUI::LoadSavedDefinitions()
{
	savedDefinitions.clear();

	std::error_code ec;
	if (!std::filesystem::exists(CUSTOM_NODES_DIR, ec))
	{
		// Create the directory if it doesn't exist
		std::filesystem::create_directories(CUSTOM_NODES_DIR, ec);
		printf("[NodeBuilder] Created custom nodes directory: %s\n", CUSTOM_NODES_DIR);
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator(CUSTOM_NODES_DIR, ec))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".json")
		{
			CustomNodeDef def;
			if (CustomNodeDef::LoadFromFile(entry.path().string(), def))
			{
				def.filePath = entry.path().string();
				savedDefinitions.push_back(def);
			}
		}
	}

	printf("[NodeBuilder] Loaded %d custom node definitions.\n", (int)savedDefinitions.size());
}

// =====================================================================
//  GetSavedDefinitions (static)
// =====================================================================

const std::vector<CustomNodeDef>& NodeBuilderUI::GetSavedDefinitions()
{
	return savedDefinitions;
}
