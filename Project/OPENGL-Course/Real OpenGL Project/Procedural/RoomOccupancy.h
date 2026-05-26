#pragma once

#include <glm/glm.hpp>
#include <vector>

// =====================================================================
// RoomOccupancy — 2D AABB Collision Grid for Interior Furniture Placement
//
// Tracks XZ footprints of placed items within a room. Every decorator
// queries CanPlace() before committing, then calls Register() to mark
// the footprint as occupied. TryPlaceAlongWall() automatically slides
// items along a wall until a clear spot is found.
//
// Inspired by 3DWorld's is_obj_placement_blocked / is_valid_placement_for_room.
// =====================================================================

struct PlacedFootprint
{
	glm::vec2 min; // XZ min corner
	glm::vec2 max; // XZ max corner
};

class RoomOccupancy
{
public:
	RoomOccupancy(glm::vec2 roomMin, glm::vec2 roomMax);

	// Check if a footprint centered at 'center' with given halfSize fits
	// without overlapping any existing item or exceeding room bounds.
	// 'padding' adds extra clearance around the candidate.
	bool CanPlace(glm::vec2 center, glm::vec2 halfSize, float padding = 0.05f) const;

	// Register an item footprint as occupied
	void Register(glm::vec2 center, glm::vec2 halfSize);

	// Block area in front of a door to prevent furniture collisions
	void BlockDoor(glm::vec3 doorPosition, float doorWidth, bool runsAlongX, float wallThickness);

	// Try to find a valid position along a wall.
	// wall: 0 = -X (left), 1 = +X (right), 2 = -Z (front), 3 = +Z (back)
	// halfSize: XZ half-extents of the item to place
	// wallOffset: distance from wall to item center (e.g. depth/2 + margin)
	// outCenter: receives the found position if successful
	// stepSize: lateral resolution for scanning (smaller = more precise, slower)
	// Returns true if a valid position was found.
	bool TryPlaceAlongWall(int wall, glm::vec2 halfSize, float wallOffset,
		glm::vec2& outCenter, float padding = 0.05f, float stepSize = 0.2f, bool preferCorner = false) const;

	// Try to place in one of the 4 corners. Returns true if a free corner was found.
	bool TryPlaceInCorner(glm::vec2 halfSize, glm::vec2& outCenter,
		float cornerOffset = 0.4f, float padding = 0.05f) const;

	// Get room bounds
	glm::vec2 GetRoomMin() const { return roomMin; }
	glm::vec2 GetRoomMax() const { return roomMax; }
	glm::vec2 GetRoomCenter() const { return (roomMin + roomMax) * 0.5f; }
	float GetRoomWidth() const { return roomMax.x - roomMin.x; }
	float GetRoomDepth() const { return roomMax.y - roomMin.y; } // y = Z in 2D

private:
	glm::vec2 roomMin, roomMax;
	std::vector<PlacedFootprint> placed;
};
