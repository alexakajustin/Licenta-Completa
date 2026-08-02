#pragma once

#include "Nodes/NodeGraph.h"
#include <string>

/**
 * @class InfiniteRepeaterNode
 * @brief Configuration Node that attaches a WorldStreamerComponent to a target GameObject, allowing for infinite terrain/grid streaming around the player.
 */
class InfiniteRepeaterNode : public GraphNode
{
public:
	InfiniteRepeaterNode(NodeGraph& graph);
	virtual ~InfiniteRepeaterNode() = default;

	virtual void RenderContent(SceneManager* scene) override;
	virtual void Execute(SceneManager& scene, NodeProgressCallback progress = nullptr) override;

	virtual json Serialize() const override;
	virtual void Deserialize(const json& j) override;

private:
	std::string targetName;
	int targetIndex;
	std::string referenceName;
	int referenceIndex;
	int radius;
};
