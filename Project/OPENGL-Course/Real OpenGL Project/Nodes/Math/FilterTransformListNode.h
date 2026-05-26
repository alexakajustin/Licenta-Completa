#pragma once

#include "Nodes/NodeGraph.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>

// =====================================================================
//  FilterTransformListNode — Filter scatter transforms by spatial rules
// =====================================================================
//
//  Percentage-based height filtering:
//    Threshold is 0–100% of the terrain's actual height range.
//    0% = lowest point, 100% = highest point.
//    Example: "Y < 40%" means "only in the bottom 40% of terrain height"
//
//  Takes a Mesh input (carrying a TransformList from ScatterNode) and
//  filters the transforms based on configurable spatial conditions.
//  Outputs the same mesh data with the filtered TransformList.
//
//  Sits between Scatter and Output in the graph:
//    SceneInput → Scatter → FilterTransforms → Output
//
//  Supports chaining multiple filters in sequence:
//    Scatter → Filter(Y < 40%) → Filter(slope > 0.8) → Output
//
class FilterTransformListNode : public GraphNode
{
public:
	FilterTransformListNode(NodeGraph& graph)
	{
		id = graph.NextNodeId();
		title = "Filter Transforms";

		// Inputs
		Pin meshIn(graph.NextPinId(), PinDataType::Mesh, "Input");
		Pin threshIn(graph.NextPinId(), PinDataType::Float, "Threshold");
		inputs.push_back(meshIn);
		inputs.push_back(threshIn);

		// Outputs
		Pin passedOut(graph.NextPinId(), PinDataType::Mesh, "Passed");
		Pin rejectedOut(graph.NextPinId(), PinDataType::Mesh, "Rejected");
		outputs.push_back(passedOut);
		outputs.push_back(rejectedOut);
	}

	json Serialize() const override
	{
		json j = GraphNode::Serialize();
		j["axis"] = axis;
		j["comparison"] = comparison;
		j["threshold"] = threshold;
		j["usePercentage"] = usePercentage;
		j["useNormalFilter"] = useNormalFilter;
		j["normalAxis"] = normalAxis;
		j["normalThreshold"] = normalThreshold;
		j["normalComparison"] = normalComparison;
		return j;
	}

	void Deserialize(const json& j) override
	{
		GraphNode::Deserialize(j);
		axis = j.value("axis", 1);
		comparison = j.value("comparison", 0);
		threshold = j.value("threshold", 50.0f);
		usePercentage = j.value("usePercentage", true);
		useNormalFilter = j.value("useNormalFilter", false);
		normalAxis = j.value("normalAxis", 1);
		normalThreshold = j.value("normalThreshold", 0.7f);
		normalComparison = j.value("normalComparison", 1);
	}

	void RenderContent(SceneManager* scene) override
	{
		// Position filter
		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Position Filter");
		
		const char* axes[] = { "X", "Y", "Z" };
		ImGui::Combo("Axis", &axis, axes, 3);

		const char* ops[] = { "<", ">", "<=", ">=" };
		ImGui::Combo("Compare", &comparison, ops, 4);

		ImGui::Checkbox("Use Percentage", &usePercentage);

		if (usePercentage)
		{
			ImGui::SliderFloat("Threshold %", &threshold, 0.0f, 100.0f, "%.1f%%");
			if (lastMinVal != FLT_MAX)
			{
				float actualThresh = lastMinVal + (threshold / 100.0f) * (lastMaxVal - lastMinVal);
				ImGui::TextDisabled("Range: %.1f to %.1f", lastMinVal, lastMaxVal);
				ImGui::TextDisabled("Actual: %.1f", actualThresh);
			}
		}
		else
		{
			ImGui::DragFloat("Threshold", &threshold, 0.5f, -10000.0f, 10000.0f);
		}

		ImGui::Separator();

		// Normal/slope filter
		ImGui::Checkbox("Slope Filter", &useNormalFilter);
		if (useNormalFilter)
		{
			ImGui::Combo("Normal Axis", &normalAxis, axes, 3);
			ImGui::Combo("Normal Cmp", &normalComparison, ops, 4);
			ImGui::DragFloat("Slope Thresh", &normalThreshold, 0.01f, -1.0f, 1.0f);
		}
	}

	void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override
	{
		outputs[0].data.Clear();
		outputs[1].data.Clear();

		// Copy through all source metadata
		outputs[0].data.type = PinDataType::Mesh;
		outputs[0].data.meshData = inputs[0].data.meshData;
		outputs[0].data.sourceObjectName = inputs[0].data.sourceObjectName;
		outputs[0].data.sourceObject = inputs[0].data.sourceObject;
		outputs[0].data.sourceMaterial = inputs[0].data.sourceMaterial;
		outputs[0].data.sourceTexture = inputs[0].data.sourceTexture;
		outputs[0].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
		outputs[0].data.textureLayers = inputs[0].data.textureLayers;

		outputs[1].data.type = PinDataType::Mesh;
		outputs[1].data.sourceObjectName = inputs[0].data.sourceObjectName;
		outputs[1].data.sourceObject = inputs[0].data.sourceObject;
		outputs[1].data.sourceMaterial = inputs[0].data.sourceMaterial;
		outputs[1].data.sourceTexture = inputs[0].data.sourceTexture;
		outputs[1].data.sourceNormalMap = inputs[0].data.sourceNormalMap;
		outputs[1].data.textureLayers = inputs[0].data.textureLayers;

		auto& inputTransforms = inputs[0].data.transforms;
		if (inputTransforms.empty())
		{
			printf("[FilterTransforms] No input transforms, nothing to filter.\n");
			return;
		}

		// Allow threshold override from connected Float pin
		float rawThresh = (inputs[1].data.type == PinDataType::Float)
			? inputs[1].data.floatValue
			: threshold;

		// === Compute actual threshold ===
		float actualThresh = rawThresh;

		if (usePercentage)
		{
			// Scan min/max of the chosen axis across ALL input transforms
			float minVal = FLT_MAX;
			float maxVal = -FLT_MAX;

			for (const auto& t : inputTransforms)
			{
				float val = GetAxisValue(t, axis);
				if (val < minVal) minVal = val;
				if (val > maxVal) maxVal = val;
			}

			// Cache for UI display
			lastMinVal = minVal;
			lastMaxVal = maxVal;

			float range = maxVal - minVal;
			if (range < 0.001f) range = 0.001f; // Avoid division by zero

			// Convert percentage (0-100) to actual value in the range
			actualThresh = minVal + (rawThresh / 100.0f) * range;

			printf("[FilterTransforms] Axis %d range: [%.1f, %.1f], %.1f%% => threshold=%.1f\n",
				axis, minVal, maxVal, rawThresh, actualThresh);
		}

		// Filter the transform list
		TransformList passed;
		TransformList rejected;
		passed.reserve(inputTransforms.size());
		rejected.reserve(inputTransforms.size() / 4);

		for (const auto& t : inputTransforms)
		{
			bool positionPass = EvaluatePosition(t, actualThresh);
			bool normalPass = useNormalFilter ? EvaluateNormal(t) : true;

			if (positionPass && normalPass)
				passed.push_back(t);
			else
				rejected.push_back(t);
		}

		outputs[0].data.transforms = std::move(passed);
		outputs[1].data.transforms = std::move(rejected);

		printf("[FilterTransforms] %d passed, %d rejected (axis=%d, op=%d, thresh=%.1f%s)\n",
			(int)outputs[0].data.transforms.size(),
			(int)outputs[1].data.transforms.size(),
			axis, comparison, actualThresh,
			usePercentage ? " [pct]" : "");
	}

private:
	// Position filter settings
	int axis = 1;             // 0=X, 1=Y, 2=Z
	int comparison = 0;       // 0=<, 1=>, 2=<=, 3=>=
	float threshold = 50.0f;  // Percentage (0-100) when usePercentage=true, absolute otherwise
	bool usePercentage = true; // Default: percentage mode

	// Slope/normal filter settings
	bool useNormalFilter = false;
	int normalAxis = 1;
	float normalThreshold = 0.7f;
	int normalComparison = 1;

	// Cached range for UI display
	mutable float lastMinVal = FLT_MAX;
	mutable float lastMaxVal = -FLT_MAX;

	static float GetAxisValue(const TransformData& t, int ax)
	{
		switch (ax)
		{
		case 0: return t.position.x;
		case 1: return t.position.y;
		case 2: return t.position.z;
		}
		return 0.0f;
	}

	bool EvaluatePosition(const TransformData& t, float thresh) const
	{
		float val = GetAxisValue(t, axis);

		switch (comparison)
		{
		case 0: return val < thresh;
		case 1: return val > thresh;
		case 2: return val <= thresh;
		case 3: return val >= thresh;
		}
		return true;
	}

	bool EvaluateNormal(const TransformData& t) const
	{
		float val = 0.0f;
		switch (normalAxis)
		{
		case 0: val = t.normal.x; break;
		case 1: val = t.normal.y; break;
		case 2: val = t.normal.z; break;
		}

		switch (normalComparison)
		{
		case 0: return val < normalThreshold;
		case 1: return val > normalThreshold;
		case 2: return val <= normalThreshold;
		case 3: return val >= normalThreshold;
		}
		return true;
	}
};
