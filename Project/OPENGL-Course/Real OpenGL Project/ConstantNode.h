#pragma once

#include "NodeGraph.h"
#include "imgui.h"

// =====================================================================
//  FloatConstantNode — Outputs a user-editable float value
// =====================================================================
class FloatConstantNode : public GraphNode
{
public:
	FloatConstantNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Float";

		Pin out(graph.NextPinId(), PinDataType::Float, "Value");
		outputs.push_back(out);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["value"] = value;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		value = j.value("value", 0.0f);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::DragFloat("##val", &value, 0.1f);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Float;
		outputs[0].data.floatValue = value;
	}

private:
	float value = 0.0f;
};

// =====================================================================
//  IntConstantNode — Outputs a user-editable integer value
// =====================================================================
class IntConstantNode : public GraphNode
{
public:
	IntConstantNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Int";

		Pin out(graph.NextPinId(), PinDataType::Int, "Value");
		outputs.push_back(out);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["value"] = value;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		value = j.value("value", 0);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::DragInt("##val", &value, 1);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Int;
		outputs[0].data.intValue = value;
	}

private:
	int value = 0;
};

// =====================================================================
//  Vec3ConstantNode — Outputs a user-editable vec3 value
// =====================================================================
class Vec3ConstantNode : public GraphNode
{
public:
	Vec3ConstantNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Vec3";

		Pin out(graph.NextPinId(), PinDataType::Vec3, "Value");
		outputs.push_back(out);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["value"] = { value.x, value.y, value.z };
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		if (j.contains("value") && j["value"].size() >= 3)
			value = glm::vec3(j["value"][0], j["value"][1], j["value"][2]);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::DragFloat3("##val", &value.x, 0.1f);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Vec3;
		outputs[0].data.vec3Value = value;
	}

private:
	glm::vec3 value = glm::vec3(0.0f);
};

// =====================================================================
//  BoolConstantNode — Outputs a user-editable boolean value
// =====================================================================
class BoolConstantNode : public GraphNode
{
public:
	BoolConstantNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Bool";

		Pin out(graph.NextPinId(), PinDataType::Bool, "Value");
		outputs.push_back(out);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["value"] = value;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		value = j.value("value", false);
	}

	void RenderContent(SceneManager* scene) override
	{
		ImGui::Checkbox("##val", &value);
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[0].data.type = PinDataType::Bool;
		outputs[0].data.boolValue = value;
	}

private:
	bool value = false;
};
