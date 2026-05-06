#include "Planet.h"
#include "PrimitiveGenerator.h"
#include "Material.h"
#include "Shader.h"

Planet::Planet(const std::string& name) : GameObject(name) {
    noise = std::make_unique<Noise3D>(params.seed);
}

Planet::~Planet() {}

void Planet::Generate() {
    MeshData data = PrimitiveGenerator::GetIcosphereData(params.subdivisions);
    
    // Scale vertices by radius
    int vertCount = data.GetVertexCount();
    for (int i = 0; i < vertCount; i++) {
        int base = i * 14;
        glm::vec3 pos(data.vertices[base], data.vertices[base + 1], data.vertices[base + 2]);
        pos *= params.radius;
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
