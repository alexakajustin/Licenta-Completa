#include "Nodes/NodeRegistry.h"
#include "Nodes/NodeGraph.h"
#include "Nodes/SceneInputNode.h"
#include "Nodes/SceneObjectsNode.h"
#include "Nodes/OutputNode.h"
#include "Nodes/CustomNode.h"
#include "Nodes/Math/ConstantNode.h"
#include "Nodes/Math/MathNode.h"
#include "Nodes/Math/CompareNode.h"
#include "Nodes/Math/BranchNode.h"
#include "Nodes/Math/MergeMeshNode.h"
#include "Nodes/Math/ForEachNode.h"
#include "Nodes/Math/FilterTransformListNode.h"
#include "Nodes/Math/ObjectIntersectionFilterNode.h"
#include "Procedural/PerlinNoiseNode.h"
#include "Procedural/HydraulicErosionNode.h"
#include "Procedural/BeautifulErosionNode.h"
#include "Procedural/RiverNode.h"
#include "Procedural/ScatterNode.h"
#include "Procedural/SpawnInstancesNode.h"
#include "Procedural/CityGridNode.h"
#include "Procedural/BuildingGenNode.h"
#include "Procedural/InteriorGenNode.h"
#include "Procedural/SolarSystemNode.h"
#include "Editor/NodeBuilderUI.h"

namespace {
	template <typename T>
	NodeTypeInfo Type(const char* id, const char* name, const char* category, const char* description, std::vector<std::string> legacy = {})
	{
		NodeTypeInfo info;
		info.id = id;
		info.name = name;
		info.category = category;
		info.description = description;
		info.legacyTitles = std::move(legacy);
		info.create = [](NodeGraph& g) -> GraphNode* { return new T(g); };
		info.cls = &typeid(T);
		return info;
	}
}

const std::vector<NodeTypeInfo>& NodeRegistry::Types()
{
	static const std::vector<NodeTypeInfo> types = {
		// Inputs: what the graph starts from
		Type<SceneInputNode>("scene_object", "Scene Object", "Input", "One scene object: its mesh, and the object itself", { "Scene Input" }),
		Type<SceneObjectsNode>("scene_objects", "Scene Objects", "Input", "A list of scene objects (picked, children of a parent, or by name), with weights and labels"),
		Type<SeedNode>("seed", "Seed", "Input", "A seed for other nodes: fixed, or a new random one every run"),
		Type<RandomValueNode>("random_value", "Random Value", "Input", "A random number between Min and Max, to drive any parameter"),
		Type<FloatConstantNode>("float", "Float", "Input", "A number", { "Float" }),
		Type<IntConstantNode>("int", "Int", "Input", "A whole number", { "Int" }),
		Type<BoolConstantNode>("bool", "Bool", "Input", "On or off", { "Bool" }),
		Type<Vec3ConstantNode>("vec3", "Vec3", "Input", "Three numbers", { "Vec3" }),

		// Terrain
		Type<PerlinNoiseNode>("perlin_noise", "Perlin Noise", "Terrain", "Displaces a mesh with fractal noise (terrain from a plane)", { "Perlin Noise" }),
		Type<HydraulicErosionNode>("hydraulic_erosion", "Hydraulic Erosion", "Terrain", "Rain droplets carving gullies, thermal slides settling the slopes", { "Hydraulic Erosion" }),
		Type<BeautifulErosionNode>("beautiful_erosion", "Beautiful Erosion", "Terrain", "Fast procedural erosion look (noise-based, no simulation)", { "Beautiful Erosion" }),
		Type<RiverNode>("river", "River", "Terrain", "Carves rivers and lakes into a terrain; their water surfaces become objects", { "River" }),

		// Points: where things go
		Type<ScatterNode>("scatter", "Scatter", "Points", "Random points on a surface", { "Scatter" }),
		Type<FilterTransformListNode>("filter_points", "Filter Points", "Points", "Keeps points by height and slope", { "Filter Transforms" }),
		Type<ObjectIntersectionFilterNode>("object_filter", "Object Filter", "Points", "Keeps points away from (or only on) other objects", { "Object Filter" }),

		// Output: what the graph makes
		Type<SpawnInstancesNode>("spawn_instances", "Spawn Instances", "Output", "Places objects on points as GPU instances (forests, rocks, grass)"),
		Type<OutputNode>("mesh_output", "Mesh Output", "Output", "Writes a mesh into a scene object", { "Output" }),

		// Mesh
		Type<MergeMeshNode>("merge_mesh", "Merge Mesh", "Mesh", "Joins any number of meshes into one", { "Merge Mesh" }),
		Type<ForEachNode>("copy_to_points", "Copy to Points", "Mesh", "Copies a mesh onto every point, merged into one mesh", { "For Each" }),

		// City
		Type<CityGridNode>("city_grid", "City Grid", "City", "Roads, sidewalks, lamps and building plots", { "City Grid" }),
		Type<BuildingGenNode>("building_gen", "Building Gen", "City", "A house or a tower on every plot", { "Building Gen" }),
		Type<InteriorGenNode>("interior_gen", "Interior Gen", "City", "Rooms, doors and furniture inside buildings", { "Interior Gen" }),

		// Space
		Type<SolarSystemNode>("solar_system", "Solar System", "Space", "A sun and procedural planets", { "Solar System" }),

		// Logic, math
		Type<MathNode>("math", "Math", "Math", "Arithmetic on two numbers", { "Math" }),
		Type<CompareNode>("compare", "Compare", "Math", "Compares two numbers", { "Compare" }),
		Type<BranchNode>("branch", "Branch", "Math", "Routes a mesh by a condition", { "Branch" }),
	};
	return types;
}

const NodeTypeInfo* NodeRegistry::Find(const std::string& id)
{
	for (const auto& t : Types()) if (t.id == id) return &t;
	return nullptr;
}

const NodeTypeInfo* NodeRegistry::FindByLegacyTitle(const std::string& title)
{
	for (const auto& t : Types())
		for (const auto& legacy : t.legacyTitles)
			if (legacy == title) return &t;
	return nullptr;
}

GraphNode* NodeRegistry::Create(const std::string& id, NodeGraph& graph)
{
	if (id.rfind("custom:", 0) == 0) {
		std::string name = id.substr(7);
		for (const auto& def : NodeBuilderUI::GetSavedDefinitions())
			if (def.name == name) return new CustomNode(graph, def);
		return nullptr;
	}
	const NodeTypeInfo* info = Find(id);
	if (!info) return nullptr;
	GraphNode* node = info->create(graph);
	node->type = info->id;
	if (node->title.empty()) node->title = info->name;
	return node;
}

void NodeRegistry::Identify(GraphNode* node)
{
	if (!node || !node->type.empty()) return;
	for (const auto& t : Types()) {
		if (t.cls && *t.cls == typeid(*node)) {
			node->type = t.id;
			if (node->title.empty()) node->title = t.name;
			return;
		}
	}
}

std::string NodeRegistry::CategoryOf(const GraphNode* node)
{
	if (!node) return "";
	if (node->type.rfind("custom:", 0) == 0) return "Custom";
	if (const NodeTypeInfo* info = Find(node->type)) return info->category;
	return "Custom";
}
