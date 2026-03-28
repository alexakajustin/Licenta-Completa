#include "CustomNode.h"
#include "SceneManager.h"
#include "imgui.h"
#include <cstdio>

// =====================================================================
//  Construction
// =====================================================================

CustomNode::CustomNode(NodeGraph& graph, const CustomNodeDef& def)
	: definition(def)
{
	id = graph.NextNodeId();
	title = def.name;

	// Create input pins from definition
	for (const auto& pinDef : def.inputDefs)
	{
		Pin pin(graph.NextPinId(), pinDef.type, pinDef.name);
		inputs.push_back(pin);
	}

	// Create output pins from definition
	for (const auto& pinDef : def.outputDefs)
	{
		Pin pin(graph.NextPinId(), pinDef.type, pinDef.name);
		outputs.push_back(pin);
	}

	// Instantiate operations
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

json CustomNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["definitionName"] = definition.name;
	
	json ops = json::array();
	for (auto* op : operationInstances)
	{
		json oj;
		oj["name"] = op->GetName();
		oj["params"] = op->GetParams(); // Uses the to_json we just added to ParamValue
		ops.push_back(oj);
	}
	j["operations"] = ops;
	return j;
}

void CustomNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	// Note: definition is usually set by factory during NodeGraph::Deserialize
	
	if (j.contains("operations"))
	{
		const auto& ops = j["operations"];
		for (size_t i = 0; i < ops.size() && i < operationInstances.size(); i++)
		{
			Operation* op = operationInstances[i];
			if (ops[i].contains("params"))
			{
				auto params = ops[i]["params"].get<std::map<std::string, ParamValue>>();
				for (const auto& p : params)
				{
					op->GetParams()[p.first] = p.second;
				}
			}
		}
	}
}

void CustomNode::RenderContent(SceneManager* scene)
{
	ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "[%s]", definition.category.c_str());

	if (operationInstances.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No operations!");
		return;
	}

	// Show a compact summary of operations
	for (int i = 0; i < (int)operationInstances.size(); i++)
	{
		Operation* op = operationInstances[i];

		ImGui::PushID(i);

		// Collapsible header for each operation
		bool open = ImGui::TreeNodeEx(op->GetName().c_str(),
			ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);

		if (open)
		{
			ImGui::PushItemWidth(100.0f);
			op->RenderUI();
			ImGui::PopItemWidth();
			ImGui::TreePop();
		}

		ImGui::PopID();
	}
}

// =====================================================================
//  MapInputsToContext — copy input pin data into the workspace
// =====================================================================

void CustomNode::MapInputsToContext(OperationContext& ctx)
{
	ctx.Clear();

	for (int i = 0; i < (int)inputs.size(); i++)
	{
		const Pin& pin = inputs[i];
		const PinData& data = pin.data;
		const std::string& pinName = pin.name;

		switch (pin.dataType)
		{
		case PinDataType::Mesh:
			// First mesh input becomes the primary mesh
			if (ctx.mesh.vertices.empty())
			{
				ctx.mesh = data.meshData;
				ctx.transforms = data.transforms;
				ctx.sourceObjectName = data.sourceObjectName;
				ctx.sourceMaterial = data.sourceMaterial;
				ctx.sourceTexture = data.sourceTexture;
				ctx.sourceNormalMap = data.sourceNormalMap;
			}
			else
			{
				// Subsequent mesh inputs go to the secondary mesh
				ctx.secondaryMesh = data.meshData;
			}
			break;

		case PinDataType::Float:
			ctx.floatVars[pinName] = data.floatValue;
			break;

		case PinDataType::Int:
			ctx.intVars[pinName] = data.intValue;
			break;

		case PinDataType::Vec3:
			ctx.vec3Vars[pinName] = data.vec3Value;
			break;

		case PinDataType::Vec2:
			ctx.vec2Vars[pinName] = data.vec2Value;
			break;

		case PinDataType::Bool:
			ctx.boolVars[pinName] = data.boolValue;
			break;

		case PinDataType::TransformList:
			ctx.transforms = data.transforms;
			break;

		default:
			break;
		}
	}
}

// =====================================================================
//  MapContextToOutputs — copy workspace results to output pins
// =====================================================================

void CustomNode::MapContextToOutputs(OperationContext& ctx)
{
	for (int i = 0; i < (int)outputs.size(); i++)
	{
		Pin& pin = outputs[i];
		PinData& data = pin.data;
		data.Clear();
		data.type = pin.dataType;

		switch (pin.dataType)
		{
		case PinDataType::Mesh:
			data.meshData = ctx.mesh;
			data.transforms = ctx.transforms;
			data.sourceObjectName = ctx.sourceObjectName;
			data.sourceMaterial = ctx.sourceMaterial;
			data.sourceTexture = ctx.sourceTexture;
			data.sourceNormalMap = ctx.sourceNormalMap;
			break;

		case PinDataType::Float:
		{
			// Try to find a float variable with the output pin's name
			auto it = ctx.floatVars.find(pin.name);
			if (it != ctx.floatVars.end())
				data.floatValue = it->second;
			break;
		}

		case PinDataType::Int:
		{
			auto it = ctx.intVars.find(pin.name);
			if (it != ctx.intVars.end())
				data.intValue = it->second;
			break;
		}

		case PinDataType::Vec3:
		{
			auto it = ctx.vec3Vars.find(pin.name);
			if (it != ctx.vec3Vars.end())
				data.vec3Value = it->second;
			break;
		}

		case PinDataType::Vec2:
		{
			auto it = ctx.vec2Vars.find(pin.name);
			if (it != ctx.vec2Vars.end())
				data.vec2Value = it->second;
			break;
		}

		case PinDataType::Bool:
		{
			auto it = ctx.boolVars.find(pin.name);
			if (it != ctx.boolVars.end())
				data.boolValue = it->second;
			break;
		}

		case PinDataType::TransformList:
			data.transforms = ctx.transforms;
			break;

		default:
			break;
		}
	}
}

// =====================================================================
//  Execute — the interpreter loop
// =====================================================================

void CustomNode::Execute(SceneManager& scene)
{
	if (operationInstances.empty()) return;

	// 1. Map input pin data → workspace
	OperationContext ctx;
	MapInputsToContext(ctx);

	// 2. Execute each operation in sequence
	for (int i = 0; i < (int)operationInstances.size(); i++)
	{
		Operation* op = operationInstances[i];
		if (!op) continue;

		op->Execute(ctx);
	}

	// 3. Map workspace → output pins
	MapContextToOutputs(ctx);
}
