#include "FluidSimulation.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace glm;

inline int Floor2Int(float val)
{
    return (int)std::floor(val);
}

FluidSimulation::FluidSimulation(int width, int height)
    : water(width, height),
      terrain(width, height),
      sediment(width, height),
      tmpSediment(width, height),
      uVel(width, height),
      vVel(width, height),
      lFlux(width, height),
      rFlux(width, height),
      tFlux(width, height),
      bFlux(width, height),
      lX(1.0f),
      lY(1.0f),
      gravity(9.81f),
      sedimentCapacityConstant(40.0f),
      dissolvingConstant(0.08f),
      depositionConstant(0.08f),
      evaporationConstant(0.002f),
      rainRate(0.05f),
      maxDelta(2.0f)
{
    // Reset velocity and flux fields
    for (uint i=0; i<uVel.size(); ++i) {
        uVel(i) = 0.0f;
        vVel(i) = 0.0f;
        lFlux(i) = 0.0f;
        rFlux(i) = 0.0f;
        tFlux(i) = 0.0f;
        bFlux(i) = 0.0f;
        water(i) = 0.0f;
        sediment(i) = 0.0f;
    }
}

FluidSimulation::~FluidSimulation() 
{
}

void FluidSimulation::makeRainGlobal(double dt)
{
    // Drop water globally instead of purely random drops
    for (uint i=0; i<water.size(); i++)
    {
        water(i) += rainRate * dt;
    }
}

void FluidSimulation::smoothTerrain()
{
    for (uint y=0; y<terrain.height(); ++y)
    {
        for (uint x=0; x<terrain.width(); ++x)
        {
            float h = getTerrain(y,x);

            float hl = getTerrain(y,x-1);
            float hr = getTerrain(y,x+1);
            float ht = getTerrain(y+1,x);
            float hb = getTerrain(y-1,x);

            float dl = h - hl;
            float dr = h - hr;

            float dt = h - ht;
            float db = h - hb;

            tmpSediment(y,x) = h;

            if ((std::abs(dl) > maxDelta || std::abs(dr) > maxDelta) && dr*dl > 0.0f)
            {
                tmpSediment(y,x) = (h+hl+hr+ht+hb)/5.0f;
            }
            else if ((std::abs(dt) > maxDelta || std::abs(db) > maxDelta) && dt*db > 0.0f)
            {
                tmpSediment(y,x) = (h+hl+hr+ht+hb)/5.0f;
            }
        }
    }

    for (uint i=0; i<terrain.size(); ++i)
    {
        terrain(i) = tmpSediment(i);
    }
}

void FluidSimulation::simulateFlow(double dt)
{
    float l = 1.0f;
    float A = 0.00005f;

    const float dx = lX;
    const float dy = lY;

    float fluxFactor = dt * A * gravity / l;

    // Outflow Flux Computation
    for (uint y=0; y<uVel.height(); ++y)
    {
        for (uint x=0; x<uVel.width(); ++x)
        {
            float dh;                               // height difference
            float h0 = terrain(y,x) + water(y,x);   // water height at current cell
            float newFlux;

            // left outflow
            if (x > 0) {
                dh = h0 - (terrain(y,x-1)+water(y,x-1));
                newFlux = lFlux(y,x) + fluxFactor*dh;
                lFlux(y,x) = std::max(0.0f,newFlux);
            } else { lFlux(y,x) = 0.0f; }

            // right outflow
            if (x < water.width()-1) {
                dh = h0 - (terrain(y,x+1)+water(y,x+1));
                newFlux = rFlux(y,x) + fluxFactor*dh;
                rFlux(y,x) = std::max(0.0f,newFlux);
            } else { rFlux(y,x) = 0.0f; }

            // bottom outflow
            if (y > 0) {
                dh = h0 - (terrain(y-1,x)+water(y-1,x));
                newFlux = bFlux(y,x) + fluxFactor*dh;
                bFlux(y,x) = std::max(0.0f,newFlux);
            } else { bFlux(y,x) = 0.0f; }

            // top outflow
            if (y < water.height()-1) {
                dh = h0 - (terrain(y+1,x)+water(y+1,x));
                newFlux = tFlux(y,x) + fluxFactor*dh;
                tFlux(y,x) = std::max(0.0f,newFlux);
            } else { tFlux(y,x) = 0.0f; }

            // scaling to ensure outflux doesn't exceed volume
            float sumFlux = lFlux(y,x)+rFlux(y,x)+bFlux(y,x)+tFlux(y,x);
            if (sumFlux > 0.0f && water(y,x) > 0.0f) {
                float K = std::min(1.0f,float((water(y,x)*dx*dy)/(sumFlux*dt)));
                rFlux(y,x) *= K;
                lFlux(y,x) *= K;
                tFlux(y,x) *= K;
                bFlux(y,x) *= K;
            } else {
                rFlux(y,x) = lFlux(y,x) = tFlux(y,x) = bFlux(y,x) = 0.0f;
            }
        }
    }

    // Update water surface and velocity field
    for (uint y=0; y<uVel.height(); ++y)
    {
        for (uint x=0; x<uVel.width(); ++x)
        {
            float inFlow = getRFlux(y,x-1) + getLFlux(y,x+1) + getTFlux(y-1,x) + getBFlux(y+1,x);
            float outFlow = getRFlux(y,x) + getLFlux(y,x) + getTFlux(y,x) + getBFlux(y,x);
            float dV = dt*(inFlow-outFlow);
            float oldWater = water(y,x);
            water(y,x) += dV/(dx*dy);
            water(y,x) = std::max(water(y,x),0.0f);
            float meanWater = 0.5f*(oldWater+water(y,x));

            if (meanWater == 0.0f)
            {
                uVel(y,x) = vVel(y,x) = 0.0f;
            }
            else
            {
                uVel(y,x) = 0.5f*(getRFlux(y,x-1)-getLFlux(y,x)-getLFlux(y,x+1)+getRFlux(y,x))/(dy*meanWater);
                vVel(y,x) = 0.5f*(getTFlux(y-1,x)-getBFlux(y,x)-getBFlux(y+1,x)+getTFlux(y,x))/(dx*meanWater);
            }
        }
    }
}

float FluidSimulation::getRFlux(int y, int x) {
    if (x<0 || x>=(int)rFlux.width()) return 0.0f;
    return rFlux(y,x);
}
float FluidSimulation::getLFlux(int y, int x) {
    if (x<0 || x>=(int)lFlux.width()) return 0.0f;
    return lFlux(y,x);
}
float FluidSimulation::getBFlux(int y, int x) {
    if (y<0 || y>=(int)bFlux.height()) return 0.0f;
    return bFlux(y,x);
}
float FluidSimulation::getTFlux(int y, int x) {
    if (y<0 || y>=(int)tFlux.height()) return 0.0f;
    return tFlux(y,x);
}
float FluidSimulation::getTerrain(int y, int x) {
    return terrain(y,x);
}
float FluidSimulation::getWater(int y, int x){
    return water(y,x);
}

void FluidSimulation::simulateErosion(double dt)
{
    float Kc = sedimentCapacityConstant;
    float Ks = dissolvingConstant;
    float Kd = depositionConstant;

    for (uint y=0; y<sediment.height(); ++y)
    {
        for (uint x=0; x<sediment.width(); ++x)
        {
            float uV = uVel(y,x);
            float vV = vVel(y,x);

            // local terrain normal based on central differences
            // Be careful to not sample out of bounds for edges
            float terR = (x < terrain.width() - 1) ? getTerrain(y, x + 1) : getTerrain(y, x);
            float terL = (x > 0) ? getTerrain(y, x - 1) : getTerrain(y, x);
            float terT = (y < terrain.height() - 1) ? getTerrain(y + 1, x) : getTerrain(y, x);
            float terB = (y > 0) ? getTerrain(y - 1, x) : getTerrain(y, x);

            vec3 normal = vec3(terL - terR, terB - terT, 2.0f);
            normal = normalize(normal);
            vec3 up(0.0f,0.0f,1.0f);
            float cosa = clamp(dot(normal,up), 0.0f, 1.0f);
            float sinAlpha = std::sin(std::acos(cosa));
            sinAlpha = std::max(sinAlpha,0.1f);

            float capacity = Kc * std::sqrt(uV*uV+vV*vV)*sinAlpha*(std::min(water(y,x),0.01f)/0.01f);
            float delta = (capacity-sediment(y,x));

            if (delta > 0.0f)
            {
                float d = Ks*delta;
                terrain(y,x)  -= d;
                water(y,x)    += d;
                sediment(y,x) += d;
            }
            else if (delta < 0.0f)
            {
                float d = Kd*delta;
                terrain(y,x)  -= d; // adding sediment to ground makes terrain height drop less? Wait, it's negative delta.
                // Wait: delta is negative. so Kd*delta is negative.
                // terrain -= (negative) -> adds height to terrain.
                water(y,x)    += d; // water becomes shallower
                sediment(y,x) += d; // sediment drops
            }
        }
    }
}

void FluidSimulation::simulateSedimentTransportation(double dt)
{
    for (uint y=0; y<sediment.height(); ++y)
    {
        for (uint x=0; x<sediment.width(); ++x)
        {
            float uV = uVel(y,x);
            float vV = vVel(y,x);

            float fromPosX = float(x) - uV*dt;
            float fromPosY = float(y) - vV*dt;

            int x0 = Floor2Int(fromPosX);
            int y0 = Floor2Int(fromPosY);
            int x1 = x0+1;
            int y1 = y0+1;

            float fX = fromPosX - x0;
            float fY = fromPosY - y0;

            x0 = clamp(x0,0,int(sediment.width()-1));
            x1 = clamp(x1,0,int(sediment.width()-1));
            y0 = clamp(y0,0,int(sediment.height()-1));
            y1 = clamp(y1,0,int(sediment.height()-1));

            float newVal = mix( mix(sediment(y0,x0),sediment(y0,x1),fX), mix(sediment(y1,x0),sediment(y1,x1),fX), fY);
            tmpSediment(y,x) = newVal;
        }
    }

    for (uint i=0; i<sediment.size(); ++i)
    {
        sediment(i) = tmpSediment(i);
    }
}

void FluidSimulation::simulateEvaporation(double dt)
{
    float Ke = evaporationConstant;
    for (uint y=0; y<water.height(); ++y)
    {
        for (uint x=0; x<water.width(); ++x)
        {
            water(y,x) = std::max(water(y,x)*(1.0f-Ke*dt), 0.0);

            if (water(y,x) < 0.005f)
            {
                water(y,x) = 0.0f;
            }
        }
    }
}

void FluidSimulation::update(double dt, bool rain, bool flood)
{
    if (rain) makeRainGlobal(dt);

    simulateFlow(dt);
    simulateErosion(dt);
    simulateSedimentTransportation(dt);
    simulateEvaporation(dt);
    smoothTerrain();
}
