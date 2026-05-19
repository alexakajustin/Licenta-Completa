#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

// =====================================================================
// InteriorStructure.h — Data Models for Procedural Interior Generation
//
// Defines room classifications, structural primitives, door descriptors,
// and floor layout data used by InteriorGenNode and its decorators.
// =====================================================================

// Room functional classification (determines which decorator runs)
enum class RoomType
{
	Corridor,
	Office,
	Bathroom,
	Bedroom,
	Kitchen,
	ServerRoom,
	Closet,
	Lobby,
	Stairwell
};

// A single subdivided room within a building floor
struct InteriorRoom
{
	glm::vec3 minBounds;       // World-space AABB min corner
	glm::vec3 maxBounds;       // World-space AABB max corner
	RoomType  type = RoomType::Office;
	int       floorIndex = 0;
	bool      hasExteriorWindow = false; // True if room touches an exterior wall

	// Helpers
	glm::vec3 GetCenter() const { return (minBounds + maxBounds) * 0.5f; }
	glm::vec3 GetSize()   const { return maxBounds - minBounds; }
	float GetArea()        const { auto s = GetSize(); return s.x * s.z; }
	float GetWidth()       const { return maxBounds.x - minBounds.x; }
	float GetDepth()       const { return maxBounds.z - minBounds.z; }
	float GetHeight()      const { return maxBounds.y - minBounds.y; }
};

// A door connecting two rooms (or a room to a hallway)
struct InteriorDoor
{
	glm::vec3 position;         // Center of the door frame
	float     width = 1.0f;     // Door opening width
	float     height = 2.4f;    // Door opening height
	bool      runsAlongX;       // True if the door opening spans the X axis
	bool      hingeOnLeft = true;
	bool      isOpen = true;
};

// A single wall segment (thin box)
struct InteriorWall
{
	glm::vec3 minBounds;
	glm::vec3 maxBounds;
};

// A high-fidelity asset placement record
struct PropPlacement
{
	std::string modelPath;      // Path to the 3D model file (e.g., "Assets/Models/toilet.obj")
	glm::vec3   position;       // Placement position (world/local coordinate)
	glm::vec3   rotation;       // Rotation (Euler angles in degrees: pitch, yaw, roll)
	glm::vec3   scale = glm::vec3(1.0f); // Scaling factor
	std::string category;       // Category description (e.g., "toilet", "bed", "desk")
};

// All interior data for one building
struct BuildingInterior
{
	glm::vec3 footprintMin;    // Building footprint AABB
	glm::vec3 footprintMax;
	int       numFloors = 1;
	float     floorHeight = 3.0f;
	float     wallThickness = 0.15f;
	float     floorThickness = 0.1f;
	bool      isCommercial = false;

	std::vector<InteriorRoom> rooms;
	std::vector<InteriorDoor> doors;
	std::vector<InteriorWall> walls;
	std::vector<PropPlacement> props;
};

// Bundled furniture dimensions for pipeline parameter passing
struct FurnitureSizes
{
	glm::vec3 bed          = glm::vec3(1.6f, 0.8f, 2.0f);
	glm::vec3 desk         = glm::vec3(1.2f, 0.75f, 0.6f);
	glm::vec3 tv           = glm::vec3(0.9f, 0.6f, 0.2f);
	glm::vec3 stove        = glm::vec3(0.8f, 0.9f, 0.6f);
	glm::vec3 fridge       = glm::vec3(0.8f, 1.8f, 0.7f);
	glm::vec3 sink         = glm::vec3(0.9f, 0.9f, 0.6f);
	glm::vec3 toilet       = glm::vec3(0.5f, 0.8f, 0.7f);
	glm::vec3 bathtub      = glm::vec3(0.8f, 0.6f, 1.7f);
	glm::vec3 sofa         = glm::vec3(1.6f, 0.8f, 0.8f);
	glm::vec3 coffeeTable  = glm::vec3(1.0f, 0.45f, 0.7f);
	glm::vec3 tvStand      = glm::vec3(1.2f, 0.5f, 0.6f);
};

