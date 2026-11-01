#include "Procedural/SolarSystemNode.h"
#include "Nodes/NodeRandom.h"
#include "Scene/SceneManager.h"
#include "Scene/Planet.h"
#include "imgui.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include <random>
#include <glm/gtc/matrix_transform.hpp>

SolarSystemNode::SolarSystemNode(NodeGraph& graph) : GraphNode(graph)
{
	seed = NodeRandom::TrueRandomSeed();
	AddInput(PinType::Mesh, "Center", "center");
	AddOutput(PinType::Mesh, "Sun Output", "sun");
	AddOutput(PinType::Points, "Planet Transforms", "planets");
	AddInt("planetCount", "Planets", &planetCount, 0.2f, 0, 100);
	AddFloat("minRadius", "Min distance", &minRadius, 1.0f, 10.0f, 10000.0f, "%.0f").Tip("Closest orbit, from the sun");
	AddFloat("maxRadius", "Max distance", &maxRadius, 1.0f, 10.0f, 10000.0f, "%.0f").Tip("Farthest orbit, from the sun");
	AddFloat("minScale", "Min planet scale", &minScale, 0.1f, 0.1f, 1000.0f, "%.1f");
	AddFloat("maxScale", "Max planet scale", &maxScale, 0.1f, 0.1f, 1000.0f, "%.1f");
	AddBool("generateSun", "Sun", &generateSun);
	AddFloat("sunScale", "Sun size", &sunScale, 1.0f, 1.0f, 10000.0f, "%.0f").ShowIf([this] { return generateSun; });
	AddSeed("seed", "Seed", &seed);
}

void SolarSystemNode::SaveState(json& j) const
{
	j["spawnedObjects"] = spawnedObjects;
}

void SolarSystemNode::LoadState(const json& j)
{
	spawnedObjects.clear();
	if (j.contains("spawnedObjects"))
		for (const auto& name : j["spawnedObjects"]) spawnedObjects.insert(name.get<std::string>());
	if (!j.contains("seed")) seed = 12345;
}

void SolarSystemNode::Execute(NodeContext& ctx)
{
	SceneManager& scene = ctx.scene;
	auto progress = [&](float pct, const std::string& msg) { ctx.Progress(pct, msg); };
	progress(0.0f, "Cleaning up old solar system...");

	// Cache previous states of existing spawned planets/sun to preserve manual inspector customizations
	std::map<std::string, glm::vec3> savedRotations;
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
			savedRotations[name] = existing->GetTransform().GetRotation();
			savedMaterials[name] = existing->GetMaterial();

			// Clean detach to avoid accidental deletion
			existing->SetMaterial(nullptr);

			scene.RemoveObject(name);
		}
	}
	spawnedObjects.clear();

	// Get center point from input 0 if connected
	glm::vec3 center(0.0f);
	const NodeValue& centerIn = In("center");
	if (centerIn.Valid()) center = centerIn.mesh.origin;

	std::mt19937 gen(seed);
	std::uniform_real_distribution<float> radDist(minRadius, maxRadius);
	std::uniform_real_distribution<float> scaleDist(minScale, maxScale);
	std::uniform_real_distribution<float> angleDist(0.0f, 360.0f);
	std::uniform_int_distribution<unsigned int> seedDist(0, 1000); // Bounded to prevent float precision loss in shader

	std::string basePrefix = "SolarSystem_" + std::to_string(id) + "_";

	progress(20.0f, "Generating Sun...");

	std::vector<std::pair<glm::vec3, float>> generatedSpheres;

	// 1. Generate Sun — exactly like the menu
	if (generateSun) {
		std::string sunName = basePrefix + "Sun";
		Planet* sun = new Planet(sunName);
		PlanetParams pParams;
		pParams.radius = sunScale;
		pParams.subdivisions = 6;
		pParams.seed = seedDist(gen);
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
			}
		}

		if (Material* mat = sun->GetMaterial()) {
			mat->SetFloat("displacementHeight", pParams.radius * 0.05f);
			mat->SetFloat("tessLevel", 8.0f);
			mat->SetFloat("tessDistance", pParams.radius * 5.0f + 200.0f);
		}

		sun->GetTransform().SetPosition(center);
		if (savedRotations.count(sunName)) {
			sun->GetTransform().SetRotation(savedRotations[sunName]);
		}

		scene.AddObject(sun);
		spawnedObjects.insert(sunName);
		ctx.Claim(sunName);
		
		generatedSpheres.push_back({sun->GetTransform().GetPosition(), pParams.radius});
	}

	progress(50.0f, "Generating Planets...");

	// 2. Generate Planets — exactly like the context menu: new Planet -> Generate
	TransformList planetTransforms;

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
		pParams.radius = s;
		pParams.seed = seedDist(gen);
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
			}
		}

		if (Material* mat = p->GetMaterial()) {
			mat->SetFloat("tessLevel", 8.0f);
			mat->SetFloat("tessDistance", s * 5.0f + 200.0f);

			// Calculate temperature based on surface-to-surface distance (closest point of planet to sun surface)
			// Sun radius is sunScale (if generated), planet radius is s.
			float safeMax = std::max(maxRadius, minRadius + 1.0f);
			float actualSunRadius = generateSun ? sunScale : 0.0f;
			float surfDist = r - actualSunRadius - s;
			
			// The minimum possible surface distance allowed by overlap check is 50.0f.
			float minSurfDist = 50.0f;
			float maxSurfDist = std::max(safeMax - actualSunRadius - s, minSurfDist + 10.0f);
			float currentSurfDist = std::max(surfDist, minSurfDist);

			float temp = 0.0f;
			float intensity = sqrt(minSurfDist / currentSurfDist);
			float minIntensity = sqrt(minSurfDist / maxSurfDist);
			if (1.0f - minIntensity > 0.0001f) {
				temp = glm::clamp((intensity - minIntensity) / (1.0f - minIntensity), 0.0f, 1.0f);
			}

			mat->SetFloat("temperature", temp);
		}

		p->GetTransform().SetPosition(pos);
		if (savedRotations.count(planetName)) {
			p->GetTransform().SetRotation(savedRotations[planetName]);
		} else {
			p->GetTransform().SetRotation(glm::vec3(0.0f));
		}
		p->GetTransform().SetScale(glm::vec3(1.0f));

		scene.AddObject(p);
		spawnedObjects.insert(planetName);
		ctx.Claim(planetName);

		TransformData td;
		td.position = p->GetTransform().GetPosition();
		td.scale = p->GetTransform().GetScale();
		td.rotation = p->GetTransform().GetRotation();
		td.normal = glm::vec3(0, 1, 0);
		planetTransforms.push_back(td);

		progress(50.0f + (50.0f * (float)i / planetCount), "Generating Planet " + std::to_string(i));
	}

	// Clean up any saved materials that were NOT reused (e.g. planet count decreased)
	for (auto& [name, mat] : savedMaterials) {
		delete mat;
	}
	savedMaterials.clear();

	if (centerIn.Valid()) Out(0, centerIn);
	Out(1, NodeValue::FromPoints(std::move(planetTransforms)));

	progress(100.0f, "Solar System Generation Complete.");
}

