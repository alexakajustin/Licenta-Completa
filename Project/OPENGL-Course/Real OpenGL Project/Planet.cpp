#include "Planet.h"
#include "PrimitiveGenerator.h"
#include "Material.h"
#include "Shader.h"

Planet::Planet(const std::string& name) : GameObject(name) {
    static Shader* sharedShader = nullptr;
    if (!sharedShader) {
        sharedShader = new Shader();
        sharedShader->CreateFromFiles("Assets/Shaders/planet.vert",
                                      "Assets/Shaders/planet_tess.tcs",
                                      "Assets/Shaders/planet_tess.tes",
                                      "Assets/Shaders/planet.frag");
    }
    Material* mat = new Material();
    mat->SetShader(sharedShader);
    mat->SetFloat("radius", params.radius);
    mat->SetFloat("temperature", 0.5f);
    mat->SetFloat("seaLevel", 0.45f);
    mat->SetFloat("sandLevel", 0.48f);
    mat->SetFloat("grassLevel", 0.6f);
    mat->SetFloat("rockLevel", 0.8f);
    mat->SetFloat("snowLevel", 0.9f);
    mat->SetFloat("noiseScale", 0.01f);
    mat->SetInt("octaves", 6);
    mat->SetFloat("persistence", 0.5f);
    mat->SetFloat("lacunarity", 2.0f);
    mat->SetFloat("displacementHeight", 5.0f);
    mat->SetFloat("tessLevel", 8.0f);
    mat->SetFloat("tessDistance", params.radius * 5.0f + 200.0f);
    SetMaterial(mat);
    noise = std::make_unique<Noise3D>(params.seed);
}

Planet::~Planet() {}

void Planet::Generate() {
    Material* mat = GetMaterial();
    if (mat) {
        mat->SetFloat("radius", params.radius);
    }
    float r = params.radius;
    if (r <= 0.0f) r = 100.0f;

    MeshData data = PrimitiveGenerator::GetIcosphereData(params.subdivisions);
    
    // Scale vertices by radius (fixes clickability/bounding box)
    int vertCount = data.GetVertexCount();
    for (int i = 0; i < vertCount; i++) {
        int base = i * 14;
        glm::vec3 pos(data.vertices[base], data.vertices[base + 1], data.vertices[base + 2]);
        pos *= r;
        data.vertices[base] = pos.x;
        data.vertices[base + 1] = pos.y;
        data.vertices[base + 2] = pos.z;
    }

    SetMesh(data.ToMesh());
    SetCPUMeshData(data);
    SetPrimitiveType("Planet");
    SetUseTessellation(true);

    if (!GetMaterial()) {
        Material* mat = new Material();
        Shader* planetShader = new Shader();
        planetShader->CreateFromFiles("Assets/Shaders/planet.vert", 
                                      "Assets/Shaders/planet_tess.tcs", 
                                      "Assets/Shaders/planet_tess.tes", 
                                      "Assets/Shaders/planet.frag");
        mat->SetShader(planetShader);
        mat->SetFloat("radius", params.radius);
        mat->SetFloat("temperature", 0.5f);
        mat->SetFloat("seaLevel", 0.45f);
        mat->SetFloat("sandLevel", 0.48f);
        mat->SetFloat("grassLevel", 0.6f);
        mat->SetFloat("rockLevel", 0.8f);
        mat->SetFloat("snowLevel", 0.9f);
        mat->SetFloat("noiseScale", 0.01f);
        mat->SetInt("octaves", 6);
        mat->SetFloat("persistence", 0.5f);
        mat->SetFloat("lacunarity", 2.0f);
        mat->SetFloat("displacementHeight", 5.0f);
        mat->SetFloat("tessLevel", 8.0f);
        mat->SetFloat("tessDistance", params.radius * 5.0f + 200.0f);
        SetMaterial(mat);
    }
    UpdateUniforms();
}

void Planet::UpdateUniforms() {
    Material* mat = GetMaterial();
    if (!mat) return;
    
    mat->SetInt("seed", params.seed);
}

void Planet::SetParams(const PlanetParams& p) {
    params = p;
    // noise is now used mainly for seed if we do CPU stuff, but for now we rely on GPU
}

void Planet::UseSunShader() {
    static Shader* sunShader = nullptr;
    if (!sunShader) {
        sunShader = new Shader();
        sunShader->CreateFromFiles("Assets/Shaders/sun.vert",
                                    "Assets/Shaders/sun_tess.tcs",
                                    "Assets/Shaders/sun_tess.tes",
                                    "Assets/Shaders/sun.frag");
    }
    if (GetMaterial()) {
        GetMaterial()->SetShader(sunShader);
    }
}
