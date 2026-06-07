#ifdef RUN_UNIT_TESTS

// Toggle this macro to run performance tests (disabled by default to avoid slow test runs)
// #define RUN_PERFORMANCE_TESTS

#ifdef RUN_PERFORMANCE_TESTS

#include "External Libs/doctest.h"
#include "Core/PerformanceProfiler.h"
#include "Core/ServiceLocator.h"
#include "Core/AssetManager.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Scene/SceneSerializer.h"
#include "Lighting/DirectionalLight.h"
#include "Lighting/PointLight.h"
#include "Lighting/SpotLight.h"
#include "Rendering/Texture.h"
#include "Rendering/Material.h"
#include "Rendering/PrimitiveGenerator.h"
#include "Procedural/ScatterNode.h"
#include "Procedural/HydraulicErosionNode.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <fstream>

TEST_CASE("RAMY Engine Performance Profiler Suite")
{
    // Initialize required dummy resources for SceneManager
    SceneManager scene;
    Texture dummyTex;
    Material dummyMat;
    scene.SetDefaultResources(&dummyTex, &dummyMat);

    NodeGraph graph;
    PerformanceProfiler& profiler = PerformanceProfiler::Get();
    profiler.Clear();

    // 1. High-density Profile of ScatterNode
    {
        std::cout << "\n==================================================\n";
        std::cout << "  Profiling ScatterNode CPU Performance (High Density)\n";
        std::cout << "==================================================\n";

        MeshData surfaceMesh = PrimitiveGenerator::GetPlaneData(200, 200); 
        MeshData objectMesh = PrimitiveGenerator::GetCubeData();

        std::vector<int> instanceCounts = { 
            100, 500, 1000, 5000, 10000, 25000, 50000, 75000, 100000, 250000, 500000, 750000, 1000000 
        };

        for (int count : instanceCounts)
        {
            ScatterNode scatterNode(graph);
            scatterNode.inputs[0].data.type = PinDataType::Mesh;
            scatterNode.inputs[0].data.meshData = surfaceMesh;

            scatterNode.inputs[1].data.type = PinDataType::Mesh;
            scatterNode.inputs[1].data.meshData = objectMesh;

            nlohmann::json j;
            j["count"] = count;
            j["seed"] = 42 + count;
            j["alignToNormal"] = true;
            j["randomRotation"] = true;
            j["spawnAsObjects"] = false; 
            scatterNode.Deserialize(j);

            profiler.Profile("ScatterNode_Execution", count, [&]() {
                scatterNode.Execute(scene);
            });

            CHECK(scatterNode.outputs[1].data.transforms.size() == (size_t)count);
        }
    }

    // 2. High-density Profile of CPU Mesh Operations (TransformBy & Append)
    {
        std::cout << "\n==================================================\n";
        std::cout << "  Profiling CPU Mesh Operations (High Density)\n";
        std::cout << "==================================================\n";

        std::vector<int> planeResolutions = { 
            10, 20, 30, 40, 50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400 
        };

        for (int res : planeResolutions)
        {
            MeshData mesh = PrimitiveGenerator::GetPlaneData(res, res);
            int vertCount = mesh.GetVertexCount();
            glm::mat4 mat = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));

            profiler.Profile("MeshData_TransformBy", vertCount, [&]() {
                mesh.TransformBy(mat);
            });
        }

        std::vector<int> appendResolutions = { 
            10, 20, 30, 40, 50, 75, 100, 125, 150, 175, 200 
        };

        for (int res : appendResolutions)
        {
            MeshData mesh1 = PrimitiveGenerator::GetPlaneData(res, res);
            MeshData mesh2 = PrimitiveGenerator::GetPlaneData(res, res);
            int combinedVertCount = mesh1.GetVertexCount() + mesh2.GetVertexCount();

            profiler.Profile("MeshData_Append", combinedVertCount, [&]() {
                mesh1.Append(mesh2);
            });
        }
    }

    // 3. High-density Profile of CPU Hydraulic Erosion Node
    {
        std::cout << "\n==================================================\n";
        std::cout << "  Profiling CPU Hydraulic Erosion (High Density)\n";
        std::cout << "==================================================\n";

        std::vector<int> gridResolutions = { 16, 24, 32, 48, 64, 80, 96, 112, 128 };
        int steps = 10; 

        for (int res : gridResolutions)
        {
            HydraulicErosionNode erosionNode(graph);
            erosionNode.SetSteps(steps);
            erosionNode.SetRainRate(1.0f);
            erosionNode.SetKs(0.9f);
            erosionNode.SetKd(0.02f);
            erosionNode.SetSmoothPasses(1);

            MeshData terrainMesh = PrimitiveGenerator::GetPlaneData(res - 1, res - 1); 
            erosionNode.inputs[0].data.type = PinDataType::Mesh;
            erosionNode.inputs[0].data.meshData = terrainMesh;

            profiler.Profile("HydraulicErosion_Execution", res * res, [&]() {
                erosionNode.Execute(scene);
            });

            CHECK(erosionNode.outputs[0].data.type == PinDataType::Mesh);
        }
    }

    // 4. Comparative Scene Loading Benchmarks
    {
        std::cout << "\n==================================================\n";
        std::cout << "  Profiling Comparative Scene Loading\n";
        std::cout << "==================================================\n";

        std::vector<std::string> scenesToLoad = {
            "Interior.json",
            "Planets.json",
            "City_Grid.json",
            "San_Miguel.json"
        };

        for (const auto& sceneFile : scenesToLoad)
        {
            std::string scenePath = "Assets/Scenes/" + sceneFile;
            std::ifstream testFile(scenePath);
            bool fileExists = testFile.good();
            testFile.close();

            if (fileExists)
            {
                DirectionalLight mainLight;
                PointLight pointLights[100];
                unsigned int pointLightCount = 0;
                SpotLight spotLights[100];
                unsigned int spotLightCount = 0;

                // Load count parameter represents the scene index or placeholder (we use 1 for single load)
                profiler.Profile("Scene_Load_" + sceneFile, 1, [&]() {
                    scene.Clear();
                    SceneSerializer::LoadScene(scenePath, scene, mainLight, pointLights, pointLightCount, spotLights, spotLightCount, &dummyTex, &dummyMat);
                });

                std::cout << "[PROFILER] " << sceneFile << " loaded with " << scene.GetObjects().size() << " objects.\n";
            }
            else
            {
                std::cout << "[PROFILER] Warning: Scene file not found: " << scenePath << ". Skipping.\n";
            }
        }
    }

    // Write CSV files to multiple potential paths to ensure it's saved in the workspace root
    std::cout << "\n==================================================\n";
    std::cout << "  Writing Performance Profiler CSV Reports\n";
    std::cout << "==================================================\n";

    const char* rendererStr = (const char*)glGetString(GL_RENDERER);
    const char* vendorStr = (const char*)glGetString(GL_VENDOR);
    std::string gpuVendor = vendorStr ? vendorStr : "UnknownVendor";
    std::string gpuRenderer = rendererStr ? rendererStr : "UnknownGPU";

    auto cleanStr = [](std::string s) {
        for (char& c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        std::string res;
        for (char c : s) {
            if (c == '_' && !res.empty() && res.back() == '_') continue;
            res += c;
        }
        return res;
    };

    std::string suffix = cleanStr(gpuVendor) + "_" + cleanStr(gpuRenderer);
    std::string baseFilename = "performance_profile_results_" + suffix + ".csv";

    profiler.WriteCSV(baseFilename);
    profiler.WriteCSV("../" + baseFilename);
    profiler.WriteCSV("../../" + baseFilename);
    profiler.WriteCSV("../../../" + baseFilename);
    profiler.WriteCSV("../../../../" + baseFilename);
    profiler.WriteCSV("c:/Users/Justin/Desktop/Licenta-Completa/" + baseFilename);
}

#endif // RUN_PERFORMANCE_TESTS

#endif // RUN_UNIT_TESTS
