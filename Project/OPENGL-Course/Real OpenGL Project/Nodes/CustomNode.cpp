#include "Nodes/CustomNode.h"
#include "Scene/SceneManager.h"
#include "imgui.h"
#include <cstdio>

// =====================================================================
//  Construction
// =====================================================================

CustomNode::CustomNode(NodeGraph& graph, const CustomNodeDef& def)
	: GraphNode(graph), definition(def)
{
	type = "custom:" + def.name;
	title = def.name;
	for (const auto& pinDef : def.inputDefs) AddInput(pinDef.type, pinDef.name, pinDef.name);
	for (const auto& pinDef : def.outputDefs) AddOutput(pinDef.type, pinDef.name, pinDef.name);
	RebuildOperations();
}

CustomNode::~CustomNode()
{
	for (auto* op : operationInstances)
		delete op;
	operationInstances.clear();
}

// =====================================================================
//  RebuildOperations — recreate operation instances from definition
// =====================================================================

void CustomNode::RebuildOperations()
{
	// Clean up old instances
	for (auto* op : operationInstances)
		delete op;
	operationInstances.clear();

	// Create new instances from definition
	for (const auto& slot : definition.operations)
	{
		Operation* op = OperationRegistry::Get().Create(slot.operationName);
		if (!op)
		{
			printf("[CustomNode] Warning: operation '%s' not found in registry, skipping.\n",
				slot.operationName.c_str());
			continue;
		}

		// Apply parameter overrides from the definition
		for (const auto& paramPair : slot.paramOverrides)
		{
			op->GetParams()[paramPair.first] = paramPair.second;
		}

		operationInstances.push_back(op);
	}

	printf("[CustomNode] Built '%s' with %d operations.\n",
		definition.name.c_str(), (int)operationInstances.size());
}

void CustomNode::SaveState(json& j) const
{
	j["definitionName"] = definition.name;
	json ops = json::array();
	for (auto* op : operationInstances)
		ops.push_back({ { "name", op->GetName() }, { "params", op->GetParams() } });
	j["operations"] = ops;
}

void CustomNode::LoadState(const json& j)
{
	if (!j.contains("operations")) return;
	const auto& ops = j["operations"];
	for (size_t i = 0; i < ops.size() && i < operationInstances.size(); i++) {
		if (!ops[i].contains("params")) continue;
		auto params = ops[i]["params"].get<std::map<std::string, ParamValue>>();
		for (const auto& p : params) operationInstances[i]->GetParams()[p.first] = p.second;
	}
}

void CustomNode::RenderContent(SceneManager* scene)
{
	ImGui::Dummy(ImVec2(220.0f, 0.0f)); // Minimum node width
	ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[%s]", definition.category.c_str());

	if (operationInstances.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No operations!");
		return;
	}

	// Show a compact summary of operations
	// We use a Child window to perfectly CLIP the hover highlights so they don't leak out
	ImGui::BeginChild("OpsClip", ImVec2(ImGui::GetFontSize() * 12.2f, 0.0f), ImGuiChildFlags_AutoResizeY);
	
	for (int i = 0; i < (int)operationInstances.size(); i++)
	{
		Operation* op = operationInstances[i];

		ImGui::PushID(i);

		// Now we can use the nice Framed look again because the Child window will clip it!
		bool open = ImGui::TreeNodeEx(op->GetName().c_str(),
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);

		if (open)
		{
			ImGui::PushItemWidth(ImGui::GetFontSize() * 6.7f);
			op->RenderUI();
			ImGui::PopItemWidth();
			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	ImGui::EndChild();
}

// =====================================================================
//  Execute — inputs into the workspace, the operations in order, the workspace into the outputs
// =====================================================================

void CustomNode::Execute(NodeContext& ctx)
{
	if (operationInstances.empty()) return;

	OperationContext work;
	work.Clear();
	MeshValue meshFrame; // The first mesh's frame, origin and look: the output mesh keeps them
	bool haveMesh = false;
	for (const Pin& pin : inputs) {
		const NodeValue& v = pin.value;
		if (!v.Valid()) continue;
		switch (pin.type) {
		case PinType::Mesh:
			if (!haveMesh) {
				haveMesh = true;
				meshFrame = v.mesh;
				work.mesh = v.mesh.Data();
				work.sourceObjectName = v.mesh.sourceObject.empty() ? std::string("(none)") : v.mesh.sourceObject;
				work.sourceMaterial = v.mesh.look.material;
				work.sourceTexture = v.mesh.look.texture;
				work.sourceNormalMap = v.mesh.look.normalMap;
				work.textureLayers = v.mesh.look.layers;
			} else {
				work.secondaryMesh = v.mesh.Data();
			}
			break;
		case PinType::Points: work.transforms = v.Points(); break;
		case PinType::Float:  work.floatVars[pin.name] = v.AsFloat(); break;
		case PinType::Int:    work.intVars[pin.name] = v.AsInt(); break;
		case PinType::Bool:   work.boolVars[pin.name] = v.AsBool(); break;
		case PinType::Vec2:   work.vec2Vars[pin.name] = v.AsVec2(); break;
		case PinType::Vec3:   work.vec3Vars[pin.name] = v.AsVec3(); break;
		default: break;
		}
	}

	for (size_t i = 0; i < operationInstances.size(); i++) {
		if (operationInstances[i]) operationInstances[i]->Execute(work);
		ctx.Progress(100.0f * (i + 1) / operationInstances.size(), operationInstances[i] ? operationInstances[i]->GetName() : "");
	}

	for (size_t i = 0; i < outputs.size(); i++) {
		const Pin& pin = outputs[i];
		switch (pin.type) {
		case PinType::Mesh: {
			MeshValue out = meshFrame;
			out.Set(std::move(work.mesh));
			out.look.material = work.sourceMaterial;
			out.look.texture = work.sourceTexture;
			out.look.normalMap = work.sourceNormalMap;
			out.look.layers = work.textureLayers;
			Out(i, NodeValue::FromMesh(std::move(out)));
			work.mesh = MeshData();
			break;
		}
		case PinType::Points: Out(i, NodeValue::FromPoints(TransformList(work.transforms))); break;
		case PinType::Float: { auto it = work.floatVars.find(pin.name); Out(i, NodeValue::Float(it != work.floatVars.end() ? it->second : 0.0f)); break; }
		case PinType::Int:   { auto it = work.intVars.find(pin.name);   Out(i, NodeValue::Int(it != work.intVars.end() ? it->second : 0)); break; }
		case PinType::Bool:  { auto it = work.boolVars.find(pin.name);  Out(i, NodeValue::Bool(it != work.boolVars.end() && it->second)); break; }
		case PinType::Vec2:  { auto it = work.vec2Vars.find(pin.name);  Out(i, NodeValue::Vec2(it != work.vec2Vars.end() ? it->second : glm::vec2(0.0f))); break; }
		case PinType::Vec3:  { auto it = work.vec3Vars.find(pin.name);  Out(i, NodeValue::Vec3(it != work.vec3Vars.end() ? it->second : glm::vec3(0.0f))); break; }
		default: break;
		}
	}
}
