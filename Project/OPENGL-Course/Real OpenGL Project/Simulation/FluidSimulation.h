#pragma once
#include <glm/glm.hpp>
#include "Core/Grid2D.h"

class FluidSimulation
{
public:
    FluidSimulation(int width, int height);
    ~FluidSimulation();

    Grid2D<float> water;
    Grid2D<float> terrain;
    Grid2D<float> sediment;
    Grid2D<float> tmpSediment;
    Grid2D<float> uVel;
    Grid2D<float> vVel;

    Grid2D<float> lFlux;
    Grid2D<float> rFlux;
    Grid2D<float> tFlux;
    Grid2D<float> bFlux;

    glm::vec2 rainPos;

    float lX;
    float lY;
    float gravity;
    
    // Erosion parameters exposed to the user
    float sedimentCapacityConstant; // Kc
    float dissolvingConstant;       // Ks
    float depositionConstant;       // Kd
    float evaporationConstant;      // Ke
    float rainRate;                 // dt*rain
    float maxDelta;                 // terrain smooth max diff

    void update(double dt, bool makeRain=true, bool flood=false);
    void simulateFlow(double dt);
    void simulateErosion(double dt);
    void simulateSedimentTransportation(double dt);
    void simulateEvaporation(double dt);

    void makeRainGlobal(double dt);
    void smoothTerrain();

    // flux access (takes care of boundaries)
    inline float getRFlux(int y, int x);
    inline float getLFlux(int y, int x);
    inline float getBFlux(int y, int x);
    inline float getTFlux(int y, int x);

    // terrain access
    inline float getTerrain(int y, int x);

    // water access
    inline float getWater(int y, int x);
};
