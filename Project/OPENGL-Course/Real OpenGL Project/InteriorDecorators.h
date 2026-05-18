#pragma once

#include "InteriorStructure.h"
#include "RoomOccupancy.h"
#include "MeshData.h"
#include "PrimitiveGenerator.h"
#include <random>
#include <vector>
#include <memory>
#include <map>

// =====================================================================
// InteriorDecorators.h — Modular Room Decoration System
//
// Each decorator is responsible for populating a specific room type
// with appropriate furniture, fixtures, and props as MeshData geometry.
//
// The decorator interface is intentionally minimal: given a room and
// an RNG, append prop geometry into a per-material mesh bucket map.
// =====================================================================

// Material key constants for batching (matches InteriorGenNode batch keys)
enum InteriorMaterialKey
{
	MAT_DRYWALL     = 200,  // Interior walls / ceiling paint
	MAT_FLOOR_TILE  = 201,  // Tiled floor (bathroom, kitchen)
	MAT_CARPET      = 202,  // Carpeted floor (office, bedroom)
	MAT_WOOD        = 203,  // Wooden furniture, desks, doors
	MAT_METAL       = 204,  // Metal props (server racks, pipes)
	MAT_FABRIC      = 205,  // Chair upholstery, curtains
	MAT_GLASS       = 206,  // Windows, partitions
	MAT_CONCRETE    = 207,  // Corridor / stairwell floor
	MAT_CEILING     = 208,  // Ceiling panels
};

// =====================================================================
// Base decorator interface
// =====================================================================
class IInteriorDecorator
{
public:
	virtual ~IInteriorDecorator() = default;

	// Append furniture/prop geometry for the given room into meshBuckets.
	// Each bucket is keyed by InteriorMaterialKey for efficient batching.
	// Any high-fidelity asset instantiation is appended as a PropPlacement.
	virtual void Decorate(
		std::map<int, MeshData>& meshBuckets,
		std::vector<PropPlacement>& props,
		const InteriorRoom& room,
		std::mt19937& rng,
		float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)
	) = 0;

protected:
	// Shared primitive helper: create a box at a world position
	static MeshData MakeBox(glm::vec3 center, glm::vec3 halfExtents, float uvScale = 1.0f);
};

// =====================================================================
// Concrete Decorators
// =====================================================================

class OfficeDecorator : public IInteriorDecorator
{
public:
	void Decorate(std::map<int, MeshData>& meshBuckets, std::vector<PropPlacement>& props, const InteriorRoom& room,
		std::mt19937& rng, float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)) override;
};

class BathroomDecorator : public IInteriorDecorator
{
public:
	void Decorate(std::map<int, MeshData>& meshBuckets, std::vector<PropPlacement>& props, const InteriorRoom& room,
		std::mt19937& rng, float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)) override;
};

class CorridorDecorator : public IInteriorDecorator
{
public:
	void Decorate(std::map<int, MeshData>& meshBuckets, std::vector<PropPlacement>& props, const InteriorRoom& room,
		std::mt19937& rng, float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)) override;
};

class BedroomDecorator : public IInteriorDecorator
{
public:
	void Decorate(std::map<int, MeshData>& meshBuckets, std::vector<PropPlacement>& props, const InteriorRoom& room,
		std::mt19937& rng, float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)) override;
};

class KitchenDecorator : public IInteriorDecorator
{
public:
	void Decorate(std::map<int, MeshData>& meshBuckets, std::vector<PropPlacement>& props, const InteriorRoom& room,
		std::mt19937& rng, float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)) override;
};

class LobbyDecorator : public IInteriorDecorator
{
public:
	void Decorate(std::map<int, MeshData>& meshBuckets, std::vector<PropPlacement>& props, const InteriorRoom& room,
		std::mt19937& rng, float floorHeight,
		glm::vec3 bedSize = glm::vec3(1.6f, 0.8f, 2.0f),
		glm::vec3 deskSize = glm::vec3(1.2f, 0.75f, 0.6f),
		glm::vec3 tvSize = glm::vec3(0.9f, 0.6f, 0.2f),
		glm::vec3 stoveSize = glm::vec3(0.8f, 0.9f, 0.6f),
		glm::vec3 fridgeSize = glm::vec3(0.8f, 1.8f, 0.7f),
		glm::vec3 sinkSize = glm::vec3(0.9f, 0.9f, 0.6f)) override;
};

// =====================================================================
// Factory: get the right decorator for a room type
// =====================================================================
std::unique_ptr<IInteriorDecorator> CreateDecoratorForRoom(RoomType type);
