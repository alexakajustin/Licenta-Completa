#pragma once

#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRandom.h"
#include "imgui.h"
#include <cmath>

// =====================================================================
// Values: constants, seeds and random numbers to drive other nodes' parameters
// =====================================================================

class FloatConstantNode : public GraphNode
{
public:
	explicit FloatConstantNode(NodeGraph& graph) : GraphNode(graph)
	{
		paramPins = false;
		AddOutput(PinType::Float, "Value", "value");
		AddFloat("value", "Value", &value, 0.1f);
	}
	void Execute(NodeContext&) override { Out(0, NodeValue::Float(value)); }

private:
	float value = 0.0f;
};

class IntConstantNode : public GraphNode
{
public:
	explicit IntConstantNode(NodeGraph& graph) : GraphNode(graph)
	{
		paramPins = false;
		AddOutput(PinType::Int, "Value", "value");
		AddInt("value", "Value", &value, 1.0f);
	}
	void Execute(NodeContext&) override { Out(0, NodeValue::Int(value)); }

private:
	int value = 0;
};

class BoolConstantNode : public GraphNode
{
public:
	explicit BoolConstantNode(NodeGraph& graph) : GraphNode(graph)
	{
		paramPins = false;
		AddOutput(PinType::Bool, "Value", "value");
		AddBool("value", "Value", &value);
	}
	void Execute(NodeContext&) override { Out(0, NodeValue::Bool(value)); }

private:
	bool value = false;
};

class Vec3ConstantNode : public GraphNode
{
public:
	explicit Vec3ConstantNode(NodeGraph& graph) : GraphNode(graph)
	{
		paramPins = false;
		AddOutput(PinType::Vec3, "Value", "value");
		AddVec3("value", "Value", &value, 0.1f);
	}
	void Execute(NodeContext&) override { Out(0, NodeValue::Vec3(value)); }

private:
	glm::vec3 value = glm::vec3(0.0f);
};

// A seed for other nodes: fixed (reproducible), or a new truly random one every run. One Seed
// node linked to many nodes re-rolls all of them at once (each still gets its own variation).
class SeedNode : public GraphNode
{
public:
	explicit SeedNode(NodeGraph& graph) : GraphNode(graph)
	{
		paramPins = false;
		AddOutput(PinType::Int, "Seed", "seed");
		AddEnum("mode", "Mode", &mode, { "Fixed", "New every run" })
			.Tip("Fixed: the same seed every run (reproducible).\nNew every run: a fresh random seed each time the graph runs.");
		AddSeed("seed", "Seed", &seed).Tip("The seed passed on. In 'New every run' mode: the last one rolled\n(switch back to Fixed to keep it).");
	}

	void Execute(NodeContext& ctx) override
	{
		if (mode == 1) seed = NodeRandom::TrueRandomSeed();
		Out(0, NodeValue::Int(seed));
		ctx.Info("seed " + std::to_string(seed));
	}

private:
	int mode = 0;
	int seed = NodeRandom::TrueRandomSeed();
};

// A random number between Min and Max, for any parameter (a count, a height, an angle...)
class RandomValueNode : public GraphNode
{
public:
	explicit RandomValueNode(NodeGraph& graph) : GraphNode(graph)
	{
		AddOutput(PinType::Float, "Value", "value");
		AddOutput(PinType::Int, "Integer", "integer");
		AddFloat("min", "Min", &minValue, 0.1f);
		AddFloat("max", "Max", &maxValue, 0.1f);
		AddSeed("seed", "Seed", &seed);
		paramPins = false;
		AddEnum("mode", "Mode", &mode, { "From seed", "New every run" })
			.Tip("From seed: the same value for the same seed.\nNew every run: a different value each time the graph runs.");
	}

	void Execute(NodeContext& ctx) override
	{
		if (mode == 1) seed = NodeRandom::TrueRandomSeed();
		float u = NodeRandom::Float01((uint32_t)seed, 0, 0x52414E44u);
		float lo = std::min(minValue, maxValue), hi = std::max(minValue, maxValue);
		float value = lo + (hi - lo) * u;
		int ilo = (int)std::ceil(lo), ihi = (int)std::floor(hi);
		int integer = ihi < ilo ? (int)std::lround(value) : ilo + std::min((int)(u * (float)(ihi - ilo + 1)), ihi - ilo);
		Out(0, NodeValue::Float(value));
		Out(1, NodeValue::Int(integer));
		char buf[64];
		snprintf(buf, sizeof(buf), "%.3g  (int %d)", value, integer);
		ctx.Info(buf);
	}

private:
	float minValue = 0.0f;
	float maxValue = 1.0f;
	int seed = NodeRandom::TrueRandomSeed();
	int mode = 0;
};
