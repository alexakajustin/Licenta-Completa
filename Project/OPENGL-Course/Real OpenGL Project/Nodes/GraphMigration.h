#pragma once

#include "External Libs/nlohmann/json.hpp"

class NodeGraph;
class SceneManager;

// Loads graphs saved before the node overhaul (no format version): node types by title, pins by
// position, and the structural changes, so every older scene keeps generating the same thing:
//   - Scatter spawned its instances itself, from the end of the filter chain hanging off it: that
//     chain now ends in a Spawn Instances node (with Scatter's erased areas and old groups)
//   - Interior Gen had one mesh pin per furniture category: now one Furniture list, with the
//     objects that were linked assigned to those categories
//   - Merge Mesh's two inputs became one multi input; Scene Input links carry objects where a
//     node now takes objects
struct GraphMigration
{
	static void LoadLegacy(NodeGraph& graph, const nlohmann::json& j, SceneManager& scene);
};
