#include "Procedural/SolarSystemNode.h"
#include "Scene/SceneManager.h"
#include "Scene/Planet.h"
#include "imgui.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include <random>
#include <glm/gtc/matrix_transform.hpp>

void SolarSystemNode::Execute(SceneManager& scene, NodeProgressCallback progress)
{
	if (progress) progress(0.0f, "Cleaning up old solar system...");

	// Cache previous states of existing spawned planets/sun to preserve manual inspector customizations
	std::map<std::string, glm::vec3> savedPositions;
	std::map<std::string, glm::vec3> savedRotations;
	std::map<std::string, glm::vec3> savedScales;
	std::map<std::string, PlanetParams> savedPlanetParams;
	std::map<std::string, Material*> savedMaterials;

	for (const auto& name : spawnedObjects) {
		GameObject* existing = nullptr;
		for (auto* obj : scene.GetObjects()) {
			if (obj && obj->GetName() == name) {
				existing = obj;
				break;
			}
		}
		if (existing) {
			savedPositions[name] = existing->GetTransform().GetPosition();
			savedRotations[name] = existing->GetTransform().GetRotation();
			savedScales[name] = existing->GetTransform().GetScale();
			savedMaterials[name] = existing->GetMaterial();
			if (Planet* pr = dynamic_cast<Planet*>(existing)) {
				savedPlanetParams[name] = pr->GetParams();
			}

			// Clean detach to avoid accidental deletion
			existing->SetMaterial(nullptr);

			scene.RemoveObject(name);
		}
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
	std::uniform_int_distribution<unsigned int> seedDist(0, 1000); // Bounded to prevent float precision loss in shader

	std::string basePrefix = "SolarSystem_" + std::to_string(id) + "_";

	if (progress) progress(20.0f, "Generating Sun...");

	std::vector<std::pair<glm::vec3, float>> generatedSpheres;

	// 1. Generate Sun — exactly like the menu
	if (generateSun) {
		std::string sunName = basePrefix + "Sun";
		Planet* sun = new Planet(sunName);
		PlanetParams pParams;
		if (savedPlanetParams.count(sunName)) {
			pParams = savedPlanetParams[sunName];
		} else {
			pParams.radius = sunScale;
			pParams.subdivisions = 6;
			pParams.seed = seedDist(gen);
		}
		sun->SetParams(pParams);

		if (savedMaterials.count(sunName) && savedMaterials[sunName]) {
			// Delete the constructor-allocated material before overwriting with saved one
			Material* constructorMat = sun->GetMaterial();
			sun->SetMaterial(savedMaterials[sunName]);
			delete constructorMat;
			savedMaterials.erase(sunName); // Mark as consumed
		}

		sun->Generate();

		if (!savedMaterials.count(sunName)) {
			sun->UseSunShader();
			if (Material* mat = sun->GetMaterial()) {
				mat->SetFloat("displacementHeight", pParams.radius * 0.05f);
				mat->SetFloat("seaLevel", 0.45f);
				mat->SetFloat("sandLevel", 0.48f);
				mat->SetFloat("grassLevel", 0.6f);
				mat->SetFloat("rockLevel", 0.8f);
				mat->SetFloat("snowLevel", 0.9f);
				mat->SetFloat("noiseScale", 1.0f);
				mat->SetInt("octaves", 6);
				mat->SetFloat("persistence", 0.5f);
				mat->SetFloat("lacunarity", 2.0f);
				mat->SetFloat("tessLevel", 8.0f);
				mat->SetFloat("tessDistance", pParams.radius * 5.0f + 200.0f);
			}
		}

		if (savedPositions.count(sunName)) {
			sun->GetTransform().SetPosition(savedPositions[sunName]);
		} else {
			sun->GetTransform().SetPosition(center);
		}
		if (savedRotations.count(sunName)) {
			sun->GetTransform().SetRotation(savedRotations[sunName]);
		}
		if (savedScales.count(sunName)) {
			sun->GetTransform().SetScale(savedScales[sunName]);
		}

		scene.AddObject(sun);
		spawnedObjects.insert(sunName);
		
		generatedSpheres.push_back({sun->GetTransform().GetPosition(), pParams.radius});
	}

	if (progress) progress(50.0f, "Generating Planets...");

	// 2. Generate Planets — exactly like the context menu: new Planet -> Generate
	outputs[1].data.transforms.clear();
	outputs[1].data.type = PinDataType::TransformList;

	std::uniform_real_distribution<float> noiseDist(0.5f, 2.5f);
	std::uniform_int_distribution<int> octavesDist(4, 8);
	std::uniform_real_distribution<float> persistDist(0.3f, 0.6f);
	std::uniform_real_distribution<float> lacunDist(4.0f, 7.0f);

	std::uniform_real_distribution<float> seaDist(0.2f, 0.6f);
	std::uniform_real_distribution<float> sandGapDist(0.01f, 0.05f);
	std::uniform_real_distribution<float> grassGapDist(0.05f, 0.2f);
	std::uniform_real_distribution<float> rockGapDist(0.1f, 0.2f);
	std::uniform_real_distribution<float> snowGapDist(0.05f, 0.15f);

	std::uniform_real_distribution<float> dispDist(3.0f, 5.0f);

	for (int i = 0; i < planetCount; ++i) {
		float r, angle, s;
		glm::vec3 pos;
		bool valid = false;
		int attempts = 0;

		while (!valid && attempts < 50) {
			r = radDist(gen);
			angle = angleDist(gen);
			s = scaleDist(gen);

			// Calculate position in orbit
			float radAngle = glm::radians(angle);
			pos = center + glm::vec3(r * cos(radAngle), 0.0f, r * sin(radAngle));

			valid = true;
			for (const auto& sphere : generatedSpheres) {
				float dist = glm::distance(pos, sphere.first);
				if (dist < (s + sphere.second + 50.0f)) { // Minimum 50 units padding
					valid = false;
					break;
				}
			}
			attempts++;
		}

		if (!valid) continue; // Skip if we couldn't find a valid non-overlapping position
		generatedSpheres.push_back({pos, s});

		std::string planetName = basePrefix + "Planet_" + std::to_string(i);
		Planet* p = new Planet(planetName);

		// Set radius before Generate() so the mesh is generated at the right size
		PlanetParams pParams;
		if (savedPlanetParams.count(planetName)) {
			pParams = savedPlanetParams[planetName];
		} else {
			pParams.radius = s;
			pParams.seed = seedDist(gen);
		}
		p->SetParams(pParams);

		if (savedMaterials.count(planetName) && savedMaterials[planetName]) {
			// Delete the constructor-allocated material before overwriting with saved one
			Material* constructorMat = p->GetMaterial();
			p->SetMaterial(savedMaterials[planetName]);
			delete constructorMat;
			savedMaterials.erase(planetName); // Mark as consumed
		}

		p->Generate();

		if (!savedMaterials.count(planetName)) {
			float seaLvl = seaDist(gen);
			float sandLvl = seaLvl + sandGapDist(gen);
			float grassLvl = sandLvl + grassGapDist(gen);
			float rockLvl = grassLvl + rockGapDist(gen);
			float snowLvl = std::min(rockLvl + snowGapDist(gen), 1.0f);

			if (Material* mat = p->GetMaterial()) {
				mat->SetInt("isSun", 0);
				mat->SetFloat("displacementHeight", 0.1);
				mat->SetFloat("seaLevel", seaLvl);
				mat->SetFloat("sandLevel", sandLvl);
				mat->SetFloat("grassLevel", grassLvl);
				mat->SetFloat("rockLevel", rockLvl);
				mat->SetFloat("snowLevel", snowLvl);
				mat->SetFloat("noiseScale", noiseDist(gen));
				mat->SetInt("octaves", octavesDist(gen));
				mat->SetFloat("persistence", persistDist(gen));
				mat->SetFloat("lacunarity", lacunDist(gen));
				mat->SetFloat("tessLevel", 8.0f);
				mat->SetFloat("tessDistance", s * 5.0f + 200.0f);

				// Calculate temperature based on distance to sun (minRadius = hot, maxRadius = cold)
				float temp = 0.5f;
				if (maxRadius > minRadius) {
					temp = 1.0f - glm::clamp((r - minRadius) / (maxRadius - minRadius), 0.0f, 1.0f);
				}
				mat->SetFloat("temperature", temp);
			}
		}

		if (savedPositions.count(planetName)) {
			p->GetTransform().SetPosition(savedPositions[planetName]);
		} else {
			p->GetTransform().SetPosition(pos);
		}
		if (savedRotations.count(planetName)) {
			p->GetTransform().SetRotation(savedRotations[planetName]);
		}
		if (savedScales.count(planetName)) {
			p->GetTransform().SetScale(savedScales[planetName]);
		} else {
			p->GetTransform().SetScale(glm::vec3(1.0f));
		}

		scene.AddObject(p);
		spawnedObjects.insert(planetName);

		TransformData td;
		td.position = p->GetTransform().GetPosition();
		td.scale = p->GetTransform().GetScale();
		td.rotation = p->GetTransform().GetRotation();
		td.normal = glm::vec3(0, 1, 0);
		outputs[1].data.transforms.push_back(td);

		if (progress) progress(50.0f + (50.0f * (float)i / planetCount), "Generating Planet " + std::to_string(i));
	}

	// Clean up any saved materials that were NOT reused (e.g. planet count decreased)
	for (auto& [name, mat] : savedMaterials) {
		delete mat;
	}
	savedMaterials.clear();

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
