#ifdef RUN_UNIT_TESTS

// The procedural node graph: values and links, parameters driven by links, seeds, multi inputs,
// saving and loading, and the conversion of graphs saved before the node overhaul.
#include "External Libs/doctest.h"
#include "Nodes/NodeGraph.h"
#include "Nodes/NodeRegistry.h"
#include "Nodes/NodeRandom.h"
#include "Nodes/SceneInputNode.h"
#include "Procedural/ScatterNode.h"
#include "Procedural/SpawnInstancesNode.h"
#include "Procedural/InteriorGenNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/PrimitiveGenerator.h"

namespace {
	// A node recording what reached its inputs
	class ProbeNode : public GraphNode
	{
	public:
		explicit ProbeNode(NodeGraph& g) : GraphNode(g)
		{
			AddInput(PinType::Points, "Points", "points", true);
			AddInput(PinType::Float, "Value", "value");
			type = "probe";
			title = "Probe";
		}
		void Execute(NodeContext&) override
		{
			pointCount = In("points").Points().size();
			sources = inputs[0].sources.size();
			value = In("value").AsFloat();
			ran++;
		}
		size_t pointCount = 0, sources = 0;
		float value = 0.0f;
		int ran = 0;
	};

	struct Fixture {
		Texture tex;
		Material mat;
		SceneManager scene;
		NodeGraph graph;
		Fixture()
		{
			scene.SetDefaultResources(&tex, &mat);
			graph.keepValues = true;
			// A flat 20 x 20 m ground (the plane spans -1..1, scaled 10) with a mesh to scatter on
			GameObject* ground = new GameObject("TestGround");
			MeshData plane = PrimitiveGenerator::GetPlaneData(16, 16);
			ground->SetCPUMeshData(plane);
			ground->GetTransform().SetScale(glm::vec3(10.0f, 1.0f, 10.0f));
			scene.AddObject(ground);
		}
		GraphNode* Add(const char* type)
		{
			GraphNode* n = NodeRegistry::Create(type, graph);
			REQUIRE(n);
			graph.AddNode(n);
			return n;
		}
		bool Link(GraphNode* a, const char* out, GraphNode* b, const char* in)
		{
			return a->Output(out) && b->Input(in) && graph.AddLink(a->Output(out)->id, b->Input(in)->id);
		}
		void Run() { graph.Execute(scene, &tex, &mat, nullptr); }
		GraphNode* Ground()
		{
			GraphNode* g = Add("scene_object");
			g->SetParamObjects("selectedName", { "TestGround" });
			g->SetParamValue("forceOriginalPrimitive", 0);
			return g;
		}
	};
}

TEST_CASE("Node graph: randomness")
{
	SUBCASE("True random seeds differ, counter streams repeat")
	{
		int a = NodeRandom::TrueRandomSeed(), b = NodeRandom::TrueRandomSeed();
		CHECK(a != b);
		CHECK(a >= 0);
		CHECK(a < NodeRandom::MAX_SEED);
		CHECK(NodeRandom::Float01(7, 123) == NodeRandom::Float01(7, 123));
		CHECK(NodeRandom::Float01(7, 123) != NodeRandom::Float01(8, 123));
	}

	SUBCASE("Scatter: same seed, same points; another seed, other points")
	{
		Fixture f;
		GraphNode* ground = f.Ground();
		GraphNode* s1 = f.Add("scatter");
		GraphNode* s2 = f.Add("scatter");
		for (GraphNode* s : { s1, s2 }) {
			s->SetParamValue("count", 500);
			s->SetParamValue("seed", 1234);
			CHECK(f.Link(ground, "mesh", s, "surface"));
		}
		f.Run();
		const TransformList& p1 = s1->outputs[0].value.Points();
		const TransformList& p2 = s2->outputs[0].value.Points();
		REQUIRE(p1.size() == 500);
		REQUIRE(p2.size() == 500);
		CHECK(p1[17].position == p2[17].position);
		CHECK(p1[499].rotation == p2[499].rotation);

		s2->SetParamValue("seed", 4321);
		f.Run();
		CHECK(s1->outputs[0].value.Points()[17].position != s2->outputs[0].value.Points()[17].position);
		// The points lie on the ground
		for (const auto& p : s1->outputs[0].value.Points()) {
			CHECK(std::abs(p.position.x) <= 10.01f);
			CHECK(std::abs(p.position.z) <= 10.01f);
		}
	}

	SUBCASE("One Seed node drives several nodes, each differently")
	{
		Fixture f;
		GraphNode* ground = f.Ground();
		GraphNode* seed = f.Add("seed");
		seed->SetParamValue("seed", 77);
		GraphNode* s1 = f.Add("scatter");
		GraphNode* s2 = f.Add("scatter");
		for (GraphNode* s : { s1, s2 }) {
			s->SetParamValue("count", 50);
			s->SetParamValue("seed", 5); // The same own seed: only the link makes them differ
			f.Link(ground, "mesh", s, "surface");
			CHECK(f.Link(seed, "seed", s, "seed"));
		}
		f.Run();
		CHECK(s1->outputs[0].value.Points()[0].position != s2->outputs[0].value.Points()[0].position);
		// Their own seed is untouched by the run
		CHECK(*(int*)s1->FindParam("seed")->target == 5);

		// Re-rolling the Seed node changes both
		glm::vec3 before = s1->outputs[0].value.Points()[0].position;
		seed->SetParamValue("seed", 78);
		f.Run();
		CHECK(s1->outputs[0].value.Points()[0].position != before);
	}
}

TEST_CASE("Node graph: links and values")
{
	SUBCASE("A link drives a parameter for one run, the parameter keeps its own value")
	{
		Fixture f;
		GraphNode* ground = f.Ground();
		GraphNode* count = f.Add("int");
		count->SetParamValue("value", 123);
		GraphNode* scatter = f.Add("scatter");
		scatter->SetParamValue("count", 10);
		f.Link(ground, "mesh", scatter, "surface");
		CHECK(f.Link(count, "value", scatter, "count"));
		f.Run();
		CHECK(scatter->outputs[0].value.Points().size() == 123);
		CHECK(*(int*)scatter->FindParam("count")->target == 10);
		CHECK(scatter->FindParam("count")->drivenText == "123");
	}

	SUBCASE("Scalars convert, other types do not; no cycles")
	{
		Fixture f;
		GraphNode* rnd = f.Add("random_value");
		GraphNode* scatter = f.Add("scatter");
		GraphNode* filter = f.Add("filter_points");
		CHECK(f.Link(rnd, "value", scatter, "count"));       // Float -> Int parameter
		CHECK(!f.Link(scatter, "points", filter, "threshold")); // Points -> Float: no
		CHECK(f.Link(scatter, "points", filter, "points"));
		GraphNode* math = f.Add("math");
		GraphNode* math2 = f.Add("math");
		CHECK(f.Link(math, "result", math2, "defaultA"));
		CHECK(!f.Link(math2, "result", math, "defaultA"));   // Would close a loop
	}

	SUBCASE("A multi input takes several links and joins their values")
	{
		Fixture f;
		GraphNode* ground = f.Ground();
		GraphNode* a = f.Add("scatter");
		GraphNode* b = f.Add("scatter");
		a->SetParamValue("count", 30);
		b->SetParamValue("count", 70);
		f.Link(ground, "mesh", a, "surface");
		f.Link(ground, "mesh", b, "surface");
		auto* probe = new ProbeNode(f.graph);
		f.graph.AddNode(probe);
		CHECK(f.Link(a, "points", probe, "points"));
		CHECK(f.Link(b, "points", probe, "points"));
		CHECK(!f.Link(b, "points", probe, "points")); // The same link twice: no
		GraphNode* value = f.Add("float");
		value->SetParamValue("value", 2.5);
		CHECK(f.Link(value, "value", probe, "value"));
		GraphNode* value2 = f.Add("float");
		CHECK(!f.Link(value2, "value", probe, "value")); // A single input is taken
		f.Run();
		CHECK(probe->ran == 1);
		CHECK(probe->sources == 2);
		CHECK(probe->pointCount == 100);
		CHECK(probe->value == doctest::Approx(2.5f));
	}
}

TEST_CASE("Node graph: proximity filter")
{
	// Dense points kept by their distance to a few other points
	Fixture f;
	GraphNode* ground = f.Ground();
	GraphNode* near = f.Add("scatter");
	near->SetParamValue("count", 4);
	GraphNode* dense = f.Add("scatter");
	dense->SetParamValue("count", 4000);
	f.Link(ground, "mesh", near, "surface");
	f.Link(ground, "mesh", dense, "surface");
	GraphNode* close = f.Add("proximity_filter");
	GraphNode* away = f.Add("proximity_filter");
	for (GraphNode* n : { close, away }) {
		CHECK(f.Link(dense, "points", n, "points"));
		CHECK(f.Link(near, "points", n, "near"));
		n->SetParamValue("inner", 1.0);
		n->SetParamValue("radius", 3.0);
		n->SetParamValue("ignoreBelow", 0.0);
	}
	away->SetParamValue("mode", 1);
	f.Run();

	const TransformList& sources = near->outputs[0].value.Points();
	auto Nearest = [&](const TransformData& p) {
		float best = 1e9f;
		for (const auto& s : sources) best = std::min(best, glm::length(glm::vec2(p.position.x - s.position.x, p.position.z - s.position.z)));
		return best;
	};
	const TransformList& kept = close->outputs[0].value.Points();
	const TransformList& dropped = close->outputs[1].value.Points();
	REQUIRE(sources.size() == 4);
	CHECK(kept.size() + dropped.size() == 4000);
	CHECK(!kept.empty());
	bool keptNear = true, innerKept = true;
	for (const auto& p : kept) keptNear = keptNear && Nearest(p) <= 3.0f + 1e-3f;
	for (const auto& p : dropped) innerKept = innerKept && Nearest(p) > 1.0f - 1e-3f;
	CHECK(keptNear);   // Nothing beyond the radius
	CHECK(innerKept);  // Everything within Inner kept

	const TransformList& open = away->outputs[0].value.Points();
	bool farOnly = true;
	for (const auto& p : open) farOnly = farOnly && Nearest(p) >= 1.0f - 1e-3f;
	CHECK(farOnly);
	CHECK(open.size() > kept.size()); // Most of the ground is far from 4 points

	// The same points pass on every run
	size_t first = kept.size();
	f.Run();
	CHECK(close->outputs[0].value.Points().size() == first);
}

TEST_CASE("Node graph: terrain layer mask")
{
	// A layer of the ground shown around a few points, nowhere else
	Fixture f;
	GraphNode* ground = f.Ground();
	GraphNode* near = f.Add("scatter");
	near->SetParamValue("count", 3);
	f.Link(ground, "mesh", near, "surface");
	GraphNode* mask = f.Add("terrain_layer_mask");
	CHECK(f.Link(near, "points", mask, "near"));
	mask->SetParamObjects("target", { "TestGround" });
	mask->SetParamValue("layer", 2);
	mask->SetParamValue("inner", 1.0);
	mask->SetParamValue("radius", 3.0);
	mask->SetParamValue("breakup", 0.0);
	mask->SetParamValue("resolution", 128);
	f.Run();

	GameObject* g = f.scene.FindObject("TestGround");
	REQUIRE(g);
	CHECK(!g->HasLayerMask(0));
	REQUIRE(g->HasLayerMask(1));
	const TransformList& sources = near->outputs[0].value.Points();
	REQUIRE(sources.size() == 3);
	const glm::vec3 s = sources[0].position;
	CHECK(g->SampleLayerMask(1, s.x, s.z) > 0.95f);          // On an object: full
	// A spot far from all three: nothing
	float bestDist = 0.0f; glm::vec2 far(0.0f);
	for (float x = -9.0f; x <= 9.0f; x += 1.0f) for (float z = -9.0f; z <= 9.0f; z += 1.0f) {
		float d = 1e9f;
		for (const auto& p : sources) d = std::min(d, glm::length(glm::vec2(x - p.position.x, z - p.position.z)));
		if (d > bestDist) { bestDist = d; far = glm::vec2(x, z); }
	}
	REQUIRE(bestDist > 3.5f);
	CHECK(g->SampleLayerMask(1, far.x, far.y) < 0.05f);
}

TEST_CASE("Node graph: saving and loading")
{
	Fixture f;
	GraphNode* ground = f.Ground();
	GraphNode* scatter = f.Add("scatter");
	GraphNode* filter = f.Add("filter_points");
	scatter->SetParamValue("count", 321);
	scatter->SetParamValue("seed", 999);
	filter->SetParamValue("threshold", 12.5);
	f.Link(ground, "mesh", scatter, "surface");
	f.Link(scatter, "points", filter, "points");
	json saved = f.graph.Serialize();
	CHECK(saved["version"] == NodeGraph::FORMAT_VERSION);

	NodeGraph loaded;
	loaded.Deserialize(saved, f.scene);
	REQUIRE(loaded.GetNodes().size() == 3);
	REQUIRE(loaded.GetLinks().size() == 2);
	GraphNode* s = loaded.FindNode(scatter->id);
	REQUIRE(s);
	CHECK(s->type == "scatter");
	CHECK(*(int*)s->FindParam("count")->target == 321);
	CHECK(*(int*)s->FindParam("seed")->target == 999);
	CHECK(s->Output("points")->id == scatter->Output("points")->id);
	CHECK(*(float*)loaded.FindNode(filter->id)->FindParam("threshold")->target == doctest::Approx(12.5f));
	// New ids never collide with loaded ones
	int fresh = loaded.NextNodeId();
	for (auto* n : loaded.GetNodes()) CHECK(n->id != fresh);
}

TEST_CASE("Node graph: graphs saved before the overhaul")
{
	Texture tex;
	Material mat;
	SceneManager scene;
	scene.SetDefaultResources(&tex, &mat);

	SUBCASE("Scatter with filters gets a Spawn Instances at the end of its chain")
	{
		json old = json::parse(R"({
			"nextId": 100,
			"nodes": [
				{ "id": 1, "title": "Scene Input", "selectedName": "Plane", "forceOriginalPrimitive": false, "inputPins": [], "outputPins": [2] },
				{ "id": 3, "title": "Scene Input", "selectedName": "Cedar A", "inputPins": [], "outputPins": [4] },
				{ "id": 5, "title": "Scatter", "count": 4000, "seed": 1101, "alignToNormal": false, "spawnAsObjects": true,
				  "createdGroupNames": ["Scatter_Instanced_5_Cedar A_Leaves"], "deletionVolumes": [ { "x": 1, "y": 2, "z": 3, "radius": 4 } ],
				  "inputPins": [6, 7], "outputPins": [8, 9] },
				{ "id": 10, "title": "Filter Transforms", "threshold": 164.0, "usePercentage": false, "inputPins": [11, 12], "outputPins": [13, 14] },
				{ "id": 15, "title": "Object Filter", "targetName": "River_Water_467", "horizontalMargin": 4.0, "inputPins": [16], "outputPins": [17, 18] }
			],
			"links": [
				{ "id": 20, "start": 2, "end": 6 },
				{ "id": 21, "start": 4, "end": 7 },
				{ "id": 22, "start": 9, "end": 11 },
				{ "id": 23, "start": 13, "end": 16 }
			]
		})");
		NodeGraph g;
		g.Deserialize(old, scene);
		REQUIRE(g.GetNodes().size() == 6);
		SpawnInstancesNode* spawn = nullptr;
		for (auto* n : g.GetNodes()) if (n->type == "spawn_instances") spawn = static_cast<SpawnInstancesNode*>(n);
		REQUIRE(spawn);
		// Fed by the Object Filter's Passed output, and by the cedar's Scene Input as an object
		auto linkedFrom = [&](Pin* in) -> GraphNode* {
			for (auto& l : g.GetLinks()) if (l.endPinId == in->id) return g.FindNodeByPinId(l.startPinId);
			return nullptr;
		};
		CHECK(linkedFrom(spawn->Input("points")) == g.FindNode(15));
		CHECK(linkedFrom(spawn->Input("objects")) == g.FindNode(3));
		// Its first run replaces (and cleans up) what the Scatter made
		CHECK(spawn->ownedGroups.count("Scatter_Instanced_5_Cedar A_Leaves"));
		// The parameters and the chain survived
		CHECK(*(int*)g.FindNode(5)->FindParam("count")->target == 4000);
		CHECK(*(float*)g.FindNode(10)->FindParam("threshold")->target == doctest::Approx(164.0f));
		auto& targets = *(std::vector<std::string>*)g.FindNode(15)->FindParam("targets")->target;
		REQUIRE(targets.size() == 1);
		CHECK(targets[0] == "River_Water_467");
		CHECK(linkedFrom(g.FindNode(10)->Input("points")) == g.FindNode(5));
		CHECK(linkedFrom(g.FindNode(5)->Input("surface")) == g.FindNode(1));
		// Saved again, the graph is in the current format
		CHECK(g.Serialize()["version"] == NodeGraph::FORMAT_VERSION);
	}

	SUBCASE("Interior Gen's furniture pins become one list with categories")
	{
		json old = json::parse(R"({
			"nextId": 50,
			"nodes": [
				{ "id": 1, "title": "Interior Gen", "seed": 42, "numBedrooms": 2, "inputPins": [2,3,4,5,6,7,8,9,10,11,12,13], "outputPins": [] },
				{ "id": 20, "title": "Scene Input", "selectedName": "bed2 97", "inputPins": [], "outputPins": [21] },
				{ "id": 22, "title": "Scene Input", "selectedName": "tumba_fur 94", "inputPins": [], "outputPins": [23] }
			],
			"links": [
				{ "id": 30, "start": 21, "end": 11 },
				{ "id": 31, "start": 23, "end": 13 }
			]
		})");
		NodeGraph g;
		g.Deserialize(old, scene);
		GraphNode* interior = g.FindNode(1);
		REQUIRE(interior);
		// The two Scene Inputs became one Scene Objects list, each object labelled with its category
		REQUIRE(g.GetNodes().size() == 2);
		GraphNode* list = nullptr;
		for (auto* n : g.GetNodes()) if (n->type == "scene_objects") list = n;
		REQUIRE(list);
		json entries = list->Serialize()["entries"];
		REQUIRE(entries.size() == 2);
		CHECK(entries[0]["name"] == "bed2 97");
		CHECK(entries[0]["label"] == "sofa");        // Input 9 was the sofa
		CHECK(entries[1]["name"] == "tumba_fur 94");
		CHECK(entries[1]["label"] == "tv_stand");    // Input 11 the TV stand
		int furnitureLinks = 0;
		for (auto& l : g.GetLinks()) if (l.endPinId == interior->Input("furniture")->id) furnitureLinks++;
		CHECK(furnitureLinks == 1);
		CHECK(*(int*)interior->FindParam("numBedrooms")->target == 2);
	}

	SUBCASE("Duplicate Scene Inputs and chained avoid filters are merged")
	{
		json old = json::parse(R"({
			"nextId": 100,
			"nodes": [
				{ "id": 1, "title": "Scene Input", "selectedName": "Plane", "forceOriginalPrimitive": false, "inputPins": [], "outputPins": [2] },
				{ "id": 3, "title": "Scene Input", "selectedName": "Plane", "forceOriginalPrimitive": false, "inputPins": [], "outputPins": [4] },
				{ "id": 5, "title": "Scatter", "inputPins": [6, 7], "outputPins": [8, 9] },
				{ "id": 10, "title": "Scatter", "inputPins": [11, 12], "outputPins": [13, 14] },
				{ "id": 15, "title": "Object Filter", "targetName": "River", "horizontalMargin": 4.0, "inputPins": [16], "outputPins": [17, 18] },
				{ "id": 19, "title": "Object Filter", "targetName": "Lake", "horizontalMargin": 4.0, "inputPins": [20], "outputPins": [21, 22] }
			],
			"links": [
				{ "id": 30, "start": 2, "end": 6 }, { "id": 31, "start": 4, "end": 11 },
				{ "id": 32, "start": 9, "end": 16 }, { "id": 33, "start": 17, "end": 20 }
			]
		})");
		NodeGraph g;
		g.Deserialize(old, scene);
		CHECK(g.FindNode(1));
		CHECK(!g.FindNode(3));  // The second Plane input is gone...
		int planeLinks = 0;
		for (auto& l : g.GetLinks()) if (l.startPinId == g.FindNode(1)->Output("mesh")->id) planeLinks++;
		CHECK(planeLinks == 2); // ...the first one feeds both scatters
		CHECK(!g.FindNode(19)); // The lake filter merged into the river filter
		auto& targets = *(std::vector<std::string>*)g.FindNode(15)->FindParam("targets")->target;
		REQUIRE(targets.size() == 2);
		CHECK(targets[1] == "Lake");
	}

	SUBCASE("Merge Mesh's two inputs land on its multi input")
	{
		json old = json::parse(R"({
			"nextId": 40,
			"nodes": [
				{ "id": 1, "title": "Scene Input", "selectedName": "A", "inputPins": [], "outputPins": [2] },
				{ "id": 3, "title": "Scene Input", "selectedName": "B", "inputPins": [], "outputPins": [4] },
				{ "id": 5, "title": "Merge Mesh", "inputPins": [6, 7], "outputPins": [8] }
			],
			"links": [ { "id": 10, "start": 2, "end": 6 }, { "id": 11, "start": 4, "end": 7 } ]
		})");
		NodeGraph g;
		g.Deserialize(old, scene);
		int merged = 0;
		for (auto& l : g.GetLinks()) if (l.endPinId == g.FindNode(5)->Input("meshes")->id) merged++;
		CHECK(merged == 2);
	}
}

#endif
