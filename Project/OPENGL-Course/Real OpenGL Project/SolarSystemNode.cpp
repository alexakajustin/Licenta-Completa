#include "SolarSystemNode.h"
#include "SceneManager.h"
#include "Planet.h"
#include "imgui.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include <random>
#include <glm/gtc/matrix_transform.hpp>

void SolarSystemNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	if (progress) progress(0.0f, "Cleaning up old solar system...");

	// Clean up old spawned objects
	for (const auto& name : spawnedObjects) {
		scene.RemoveObject(name);
	}
	spawnedObjects.clear();

	// Get center point from input 0 if connected
	glm::vec3 center(0.0f);
	if (!inputs[0].data.transforms.empty()) {
		center = inputs[0].data.transforms[0].position;
	}

	std::mt19937 gen(seed);
	std::uniform_real_distribution<float> radDist(minRadius, maxRadius);
	std::uniform_real_distribution<float> scaleDist(minScale, maxScale);
	std::uniform_real_distribution<float> angleDist(0.0f, 360.0f);
	std::uniform_int_distribution<unsigned int> seedDist(0, 0xFFFFFFFF);

	std::string basePrefix = "SolarSystem_" + std::to_string(id) + "_";

	if (progress) progress(20.0f, "Generating Sun...");

	// 1. Generate Sun
	if (generateSun) {
		std::string sunName = basePrefix + "Sun";
		Planet* sun = new Planet(sunName);
		
		PlanetParams pParams;
		pParams.radius = sunScale;
		pParams.subdivisions = 6;
		pParams.seed = seedDist(gen);
		sun->SetParams(pParams);
		
		sun->GetTransform().SetPosition(center);
		sun->Generate(); 
		
		scene.AddObject(sun);
		spawnedObjects.insert(sunName);
	}

	if (progress) progress(50.0f, "Generating Planets...");

	// 2. Generate Planets
	outputs[1].data.transforms.clear();
	outputs[1].data.transforms.resize(planetCount);
	outputs[1].data.type = PinDataType::TransformList;

	for (int i = 0; i < planetCount; ++i) {
		float r = radDist(gen);
		float angle = angleDist(gen);
		float s = scaleDist(gen);
		unsigned int pSeed = seedDist(gen);

		// Calculate position in orbit
		float radAngle = glm::radians(angle);
		glm::vec3 pos = center + glm::vec3(r * cos(radAngle), 0.0f, r * sin(radAngle));

		// Generate Planet Object
		std::string planetName = basePrefix + "Planet_" + std::to_string(i);
		Planet* p = new Planet(planetName);
		
		PlanetParams pParams;
		pParams.radius = s;
		pParams.subdivisions = 5;
		pParams.seed = pSeed;
		p->SetParams(pParams);
		
		p->GetTransform().SetPosition(pos);
		p->Generate();
		
		scene.AddObject(p);
		spawnedObjects.insert(planetName);

		// Output transform data
		TransformData td;
		td.position = pos;
		td.scale = glm::vec3(s);
		td.rotation = glm::vec3(0.0f);
		td.normal = glm::vec3(0, 1, 0);
		outputs[1].data.transforms[i] = td;

		if (progress) progress(50.0f + (50.0f * (float)i / planetCount), "Generating Planet " + std::to_string(i));
	}

	// Pass through input 0 to output 0
	outputs[0].data = inputs[0].data;

	if (progress) progress(100.0f, "Solar System Generation Complete.");
}

void SolarSystemNode::RenderContent(SceneManager* scene)
{
	ImGui::PushID(this);

	ImGui::DragInt("Planet Count", &planetCount, 1.0f, 0, 100);
	ImGui::DragFloat("Min Radius", &minRadius, 1.0f, 10.0f, 10000.0f);
	ImGui::DragFloat("Max Radius", &maxRadius, 1.0f, 10.0f, 10000.0f);
	ImGui::DragFloat("Min Scale", &minScale, 0.1f, 0.1f, 1000.0f);
	ImGui::DragFloat("Max Scale", &maxScale, 0.1f, 0.1f, 1000.0f);
	
	ImGui::Checkbox("Generate Sun", &generateSun);
	if (generateSun) {
		ImGui::DragFloat("Sun Scale", &sunScale, 1.0f, 1.0f, 10000.0f);
	}

	if (ImGui::InputInt("Seed", &seed)) {}
	ImGui::SameLine();
	if (ImGui::Button("Rand")) {
		seed = std::rand();
	}

	ImGui::PopID();
}

void SolarSystemNode::OnRemove(SceneManager& scene)
{
	for (const auto& name : spawnedObjects) {
		scene.RemoveObject(name);
	}
	spawnedObjects.clear();
}

json SolarSystemNode::Serialize() const
{
	json j = GraphNode::Serialize();
	j["planetCount"] = planetCount;
	j["minRadius"] = minRadius;
	j["maxRadius"] = maxRadius;
	j["minScale"] = minScale;
	j["maxScale"] = maxScale;
	j["seed"] = seed;
	j["generateSun"] = generateSun;
	j["sunScale"] = sunScale;

	json sObjs = json::array();
	for (const auto& name : spawnedObjects) {
		sObjs.push_back(name);
	}
	j["spawnedObjects"] = sObjs;

	return j;
}

void SolarSystemNode::Deserialize(const json& j)
{
	GraphNode::Deserialize(j);
	planetCount = j.value("planetCount", 5);
	minRadius = j.value("minRadius", 150.0f);
	maxRadius = j.value("maxRadius", 1500.0f);
	minScale = j.value("minScale", 5.0f);
	maxScale = j.value("maxScale", 30.0f);
	seed = j.value("seed", 12345);
	generateSun = j.value("generateSun", true);
	sunScale = j.value("sunScale", 100.0f);

	spawnedObjects.clear();
	if (j.contains("spawnedObjects")) {
		for (const auto& name : j["spawnedObjects"]) {
			spawnedObjects.insert(name.get<std::string>());
		}
	}
}
