#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include <sol/sol.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "Core/ScriptEngine.h"
#include "Core/ScriptApi.h"
#include "Core/ServiceLocator.h"
#include "Scene/GameObject.h"
#include "Scene/SceneManager.h"
#include "Scene/LuaScriptComponent.h"
#include "Rendering/Material.h"
#include "Rendering/TextureLayer.h"
#include "Scene/BoxCollider.h"
#include "Lighting/LightObject.h"
#include "Lighting/PointLight.h"
#include "Editor/ScriptAnalysis.h"
#include "Editor/ScriptReference.h"

// Script Editor: Lua API metadata, type inference, completion and the compiler's diagnostics.

namespace
{
	void ReadyEngine()
	{
		ScriptEngine::GetInstance().Init();
	}

	ScriptAnalysis::Environment TestEnv()
	{
		ScriptAnalysis::Environment env;
		env.objectNames = { "Cube", "Terrain", "Main Camera" };
		env.scriptFiles = { "Assets/scripts/camera_follow.lua", "Assets/scripts/rotate_test.lua" };
		env.textureFiles = { "Assets/Textures/grid_prototype.png" };
		return env;
	}

	bool HasCompletion(const std::vector<ScriptAnalysis::Completion>& items, const std::string& label)
	{
		return std::any_of(items.begin(), items.end(), [&](const ScriptAnalysis::Completion& c) { return c.label == label; });
	}

	bool HasDiagnostic(const ScriptAnalysis::CompileResult& r, int line, const std::string& fragment)
	{
		return std::any_of(r.diagnostics.begin(), r.diagnostics.end(), [&](const ScriptAnalysis::Diagnostic& d) {
			return d.line == line && d.message.find(fragment) != std::string::npos;
		});
	}

	std::string Dump(const ScriptAnalysis::CompileResult& r)
	{
		std::string s;
		for (const auto& d : r.diagnostics) s += "  line " + std::to_string(d.line) + ": " + d.message + "\n";
		return s;
	}
}

TEST_CASE("ScriptApi - every bound type is documented and the runtime matches the metadata")
{
	ReadyEngine();
	auto& reg = ScriptApi::Registry::Get();
	for (const char* type : { "GameObject", "Transform", "Material", "TextureLayer", "Texture", "RigidBody", "BoxCollider",
							  "CapsuleCollider", "MeshCollider", "Player", "LuaScript", "vec2", "vec3", "vec4", "Scene", "Assets", "Script", "Input" })
	{
		INFO(type);
		REQUIRE(reg.FindType(type) != nullptr);
	}
	CHECK(reg.FindMember("RigidBody", "GetGameObject") != nullptr); // inherited from Component
	CHECK(reg.FindMember("GameObject", "material")->type == "Material");
	CHECK(reg.FindMember("GameObject", "GetLayer")->type == "TextureLayer");

	// Every documented member of the usertypes exists in Lua
	sol::state& lua = ScriptEngine::GetInstance().GetState();
	for (const char* type : { "GameObject", "Transform", "Material", "TextureLayer", "RigidBody", "Player" })
	{
		sol::table meta = lua[type];
		REQUIRE(meta.valid());
	}
}

TEST_CASE("ScriptApi - transform, material, texture layers and scripts are reachable from Lua")
{
	ReadyEngine();
	sol::state& lua = ScriptEngine::GetInstance().GetState();

	SceneManager scene;
	ScopedSceneManagerScope scope(&scene);
	GameObject* cube = new GameObject("ScriptApiCube");
	Material* mat = new Material(0.5f, 16.0f);
	cube->SetMaterial(mat);
	TextureLayer layer;
	layer.opacity = 0.25f;
	cube->AddTextureLayer(layer);
	scene.AddObject(cube);

	sol::protected_function_result r = lua.safe_script(R"(
		local cube = Scene.Find("ScriptApiCube")
		assert(cube ~= nil, "Scene.Find")
		cube.position = vec3(1, 2, 3)
		cube.transform:Translate(vec3(1, 0, 0))
		cube.material.color = vec4(0.1, 0.2, 0.3, 1.0)
		cube.material.shininess = 64
		local layer = cube:GetLayer(0)
		assert(math.abs(layer.opacity - 0.25) < 1e-5, "layer read")
		layer.opacity = 0.75
		layer.blendMode = LayerBlendMode.Height
		assert(#cube.layers == 1, "layers array")
		assert(cube:GetComponent("RigidBody") == nil, "no rigidbody")
		assert(vec3.new(1, 2, 3).y == 2, "vec3.new")
		apiTestResult = cube.position.x
	)", sol::script_pass_on_error);
	std::string err1; if (!r.valid()) { sol::error e = r; err1 = e.what(); }
	INFO(err1);
	REQUIRE(r.valid());
	CHECK(lua["apiTestResult"].get<float>() == doctest::Approx(2.0f));
	CHECK(cube->GetTransform().GetPosition().y == doctest::Approx(2.0f));
	CHECK(mat->GetShininess() == doctest::Approx(64.0f));
	CHECK(mat->GetColor().z == doctest::Approx(0.3f));
	CHECK(cube->GetTextureLayers()[0].opacity == doctest::Approx(0.75f));
	CHECK(cube->GetTextureLayers()[0].blendMode == LayerBlendMode::Height);

	// Another script's functions, reached through GetScript once it runs
	const std::string path = "Assets/scripts/__test_other_script.lua";
	{ std::ofstream f(path); f << "health = 42\nfunction Damage(n) health = health - n end\n"; }
	auto* comp = cube->AddComponent<LuaScriptComponent>(path);
	comp->ReloadScript();
	sol::protected_function_result r2 = lua.safe_script(R"(
		local other = Scene.Find("ScriptApiCube"):GetScript("__test_other_script")
		other.Damage(2)
		otherHealth = other.health
	)", sol::script_pass_on_error);
	std::string err2; if (!r2.valid()) { sol::error e = r2; err2 = e.what(); }
	INFO(err2);
	CHECK(r2.valid());
	CHECK(lua["otherHealth"].get<int>() == 40);
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST_CASE("ScriptApi - nested writes reach the object (transform.rotation.x = ..., material.color.r, collider.size.x)")
{
	ReadyEngine();
	sol::state& lua = ScriptEngine::GetInstance().GetState();
	SceneManager scene;
	ScopedSceneManagerScope scope(&scene);
	GameObject* box = new GameObject("NestedWriteBox");
	box->SetMaterial(new Material(0.5f, 16.0f));
	box->AddComponent<BoxCollider>();
	scene.AddObject(box);
	PointLight* pl = new PointLight();
	LightObject* lamp = new LightObject("TestLamp", pl);
	scene.AddLight(lamp);

	ScriptEngine::GetInstance().ResetClock();
	ScriptEngine::GetInstance().BeginFrame(0.5f);
	sol::protected_function_result r = lua.safe_script(R"(
		local b = Scene.Find("NestedWriteBox")
		b.transform.rotation.x = b.transform.rotation.x + 1
		b.transform.rotation.x = b.transform.rotation.x + 1
		b.position.y = 7
		b.material.color.r = 0.25
		b:GetBoxCollider().size.x = 3
		local snapshot = b.position:Copy()
		b.position.y = 9
		snapY = snapshot.y
		local lamp = Scene.FindLight("TestLamp")
		lamp.color.g = 0.5
		lamp.diffuseIntensity = 2
		lamp.position.z = 4
		dtSeen = Time.deltaTime
		framesSeen = Time.frameCount
	)", sol::script_pass_on_error);
	std::string err; if (!r.valid()) { sol::error e = r; err = e.what(); }
	INFO(err);
	REQUIRE(r.valid());
	CHECK(box->GetTransform().GetRotation().x == doctest::Approx(2.0f));
	CHECK(box->GetTransform().GetPosition().y == doctest::Approx(9.0f));
	CHECK(lua["snapY"].get<float>() == doctest::Approx(7.0f));
	CHECK(box->GetMaterial()->GetColor().r == doctest::Approx(0.25f));
	CHECK(box->GetComponent<BoxCollider>()->size.x == doctest::Approx(3.0f));
	CHECK(lamp->GetColorPtr()->g == doctest::Approx(0.5f));
	CHECK(*lamp->GetDiffuseIntensityPtr() == doctest::Approx(2.0f));
	CHECK(lamp->GetPositionPtr()->z == doctest::Approx(4.0f));
	CHECK(lua["dtSeen"].get<float>() == doctest::Approx(0.5f));
	CHECK(lua["framesSeen"].get<int>() == 1);
}

TEST_CASE("ScriptAnalysis - type inference follows member chains, calls and locals")
{
	ReadyEngine();
	auto env = TestEnv();
	ScriptAnalysis::DocumentModel doc;
	doc.Build(
		"local cube = Scene.Find(\"Cube\")\n"
		"local tr = cube.transform\n"
		"local layer = cube:GetLayer(0)\n"
		"local rb = cube:GetComponent(\"RigidBody\")\n"
		"local p = tr.position + vec3(0, 1, 0)\n"
		"for i, l in ipairs(cube.layers) do\n"
		"end\n"
		"function Update(dt)\n"
		"end\n", &env);
	CHECK(doc.InferVariable("cube", 10) == "GameObject");
	CHECK(doc.InferVariable("tr", 10) == "Transform");
	CHECK(doc.InferVariable("layer", 10) == "TextureLayer");
	CHECK(doc.InferVariable("rb", 10) == "RigidBody");
	CHECK(doc.InferVariable("p", 10) == "vec3");
	CHECK(doc.InferVariable("l", 6) == "TextureLayer");
	CHECK(doc.InferVariable("dt", 8) == "number");
	CHECK(doc.InferExpression("Scene.Find(\"Cube\"):GetLayer(0).texture", 10) == "Texture");
	CHECK(doc.InferExpression("gameObject.material.color", 10) == "vec4");
	CHECK(doc.InferExpression("vec3.new(1, 2, 3)", 10) == "vec3");
	CHECK(doc.InferExpression("vec3(1, 2, 3):Cross(vec3(0, 1, 0))", 10) == "vec3");
}

TEST_CASE("ScriptAnalysis - completion offers members, methods after ':' and scene names in strings")
{
	ReadyEngine();
	auto env = TestEnv();
	ScriptAnalysis::DocumentModel doc;
	ScriptAnalysis::CompletionContext ctx;
	std::vector<ScriptAnalysis::Completion> items;

	doc.Build("local cube = Scene.Find(\"Cube\")\ncube.\n", &env);
	REQUIRE(ScriptAnalysis::Complete(doc, 1, 5, false, ctx, items));
	CHECK(ctx.member);
	CHECK(ctx.ownerType == "GameObject");
	CHECK(HasCompletion(items, "transform"));
	CHECK(HasCompletion(items, "material"));
	CHECK(HasCompletion(items, "GetLayer"));

	doc.Build("local cube = Scene.Find(\"Cube\")\ncube:Get\n", &env);
	REQUIRE(ScriptAnalysis::Complete(doc, 1, 8, false, ctx, items));
	CHECK(HasCompletion(items, "GetLayer"));
	CHECK_FALSE(HasCompletion(items, "transform")); // properties are not offered after ':'
	CHECK(items.front().label.rfind("Get", 0) == 0);

	doc.Build("local c = Scene.Find(\"Te\n", &env);
	REQUIRE(ScriptAnalysis::Complete(doc, 0, 23, false, ctx, items));
	CHECK(ctx.inString);
	CHECK(items.front().label == "Terrain");

	doc.Build("gameObject.transform.pos\n", &env);
	REQUIRE(ScriptAnalysis::Complete(doc, 0, 24, false, ctx, items));
	CHECK(items.front().label == "position");

	env.lightNames = { "Sun" };
	doc.Build("local s = Scene.FindLight(\"S\n", &env);
	REQUIRE(ScriptAnalysis::Complete(doc, 0, 28, false, ctx, items));
	CHECK(items.front().label == "Sun");

	doc.Build("-- cube.\n", &env);
	CHECK_FALSE(ScriptAnalysis::Complete(doc, 0, 8, false, ctx, items)); // inside a comment
}

TEST_CASE("ScriptAnalysis - compiler reports syntax errors with the right line")
{
	ReadyEngine();
	auto env = TestEnv();
	ScriptAnalysis::DocumentModel doc;
	std::string code = "function Start()\n  local x = 1\n  if x then\n    log(\"x\")\nend\n";
	doc.Build(code, &env);
	auto r = ScriptAnalysis::Compile(code, "t.lua", doc);
	INFO(Dump(r));
	CHECK_FALSE(r.syntaxOk);
	CHECK(r.errors == 1);
	CHECK(HasDiagnostic(r, 1, "never closed"));

	code = "local a = = 2\n";
	doc.Build(code, &env);
	r = ScriptAnalysis::Compile(code, "t.lua", doc);
	CHECK(r.errors == 1);
	CHECK(r.diagnostics.front().line == 1);
}

TEST_CASE("ScriptAnalysis - compiler warns about engine mistakes")
{
	ReadyEngine();
	auto env = TestEnv();
	ScriptAnalysis::DocumentModel doc;
	std::string code =
		"local cube = Scene.Find(\"Cube\")\n"          // 1 ok
		"local ghost = Scene.Find(\"Cub\")\n"          // 2 missing object
		"cube.transform:SetPos(vec3(0))\n"             // 3 unknown member
		"cube.transform.SetPosition(vec3(0))\n"        // 4 method with '.'
		"local rb = cube:GetComponent(\"RigidBdy\")\n" // 5 unknown component
		"local s = Script.Require(\"nope\")\n"         // 6 missing script
		"print(helth)\n"                               // 7 unknown variable
		"function update(dt) end\n";                   // 8 wrong callback case
	doc.Build(code, &env);
	auto r = ScriptAnalysis::Compile(code, "t.lua", doc);
	INFO(Dump(r));
	CHECK(r.syntaxOk);
	CHECK(r.errors == 0);
	CHECK_FALSE(HasDiagnostic(r, 1, ""));
	CHECK(HasDiagnostic(r, 2, "did you mean 'Cube'"));
	CHECK(HasDiagnostic(r, 3, "did you mean 'SetPosition'"));
	CHECK(HasDiagnostic(r, 4, "call it with ':'"));
	CHECK(HasDiagnostic(r, 5, "Unknown component type"));
	CHECK(HasDiagnostic(r, 6, "No script named 'nope'"));
	CHECK(HasDiagnostic(r, 7, "Unknown variable 'helth'"));
	CHECK(HasDiagnostic(r, 8, "case-sensitive"));
}

TEST_CASE("ScriptAnalysis - the project's own scripts compile without false alarms")
{
	ReadyEngine();
	auto env = TestEnv();
	std::error_code ec;
	for (auto& entry : std::filesystem::directory_iterator("Assets/scripts", ec))
	{
		if (entry.path().extension() != ".lua") continue;
		std::ifstream f(entry.path());
		std::stringstream ss;
		ss << f.rdbuf();
		ScriptAnalysis::DocumentModel doc;
		doc.Build(ss.str(), &env);
		auto r = ScriptAnalysis::Compile(ss.str(), entry.path().filename().string(), doc);
		MESSAGE(entry.path().filename().string() << ": " << r.errors << " errors, " << r.warnings << " warnings, " << r.milliseconds << " ms\n" << Dump(r));
		CHECK(r.syntaxOk);
	}
}

TEST_CASE("ScriptReference - drag payloads produce the code the editor inserts")
{
	GameObject go("Main Cube");
	CHECK(ScriptReference::ObjectExpression(&go) == "Scene.Find(\"Main Cube\")");
	CHECK(ScriptReference::MakeVariableName("Main Cube", "Transform") == "mainCubeTransform");
	CHECK(ScriptReference::MakeVariableName("end") == "endRef");
	ScriptReference::Payload p;
	REQUIRE(ScriptReference::DescribeAsset("C:\\proj\\Assets\\Textures\\rock_01.png", p));
	CHECK(std::string(p.expression) == "Assets.Texture(\"Assets/Textures/rock_01.png\")");
	CHECK(std::string(p.variable) == "rock01Texture");
	REQUIRE(ScriptReference::DescribeAsset("Assets/scripts/enemy_ai.lua", p));
	CHECK(std::string(p.expression) == "Script.Require(\"enemy_ai\")");
}

#endif // RUN_UNIT_TESTS
