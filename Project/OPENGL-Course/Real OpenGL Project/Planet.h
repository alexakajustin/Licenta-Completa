#pragma once

#include "GameObject.h"
#include "Noise3D.h"
#include <memory>

struct PlanetParams {
    float radius = 100.0f;
    int subdivisions = 6;
    unsigned int seed = 12345;
};

class Planet : public GameObject {
public:
    Planet(const std::string& name = "Planet");
    ~Planet();

    void Generate();
    void UpdateUniforms();
    void SetParams(const PlanetParams& params);
    const PlanetParams& GetParams() const { return params; }
    void UseSunShader();

private:
    PlanetParams params;
    std::unique_ptr<Noise3D> noise;
};
