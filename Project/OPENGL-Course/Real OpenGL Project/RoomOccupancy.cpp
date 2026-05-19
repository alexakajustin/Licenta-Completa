#include "RoomOccupancy.h"
#include <algorithm>

// =====================================================================
// RoomOccupancy Implementation
// =====================================================================

RoomOccupancy::RoomOccupancy(glm::vec2 roomMin, glm::vec2 roomMax)
	: roomMin(roomMin), roomMax(roomMax)
{
}

bool RoomOccupancy::CanPlace(glm::vec2 center, glm::vec2 halfSize, float padding) const
{
	glm::vec2 candidateMin = center - halfSize - glm::vec2(padding);
	glm::vec2 candidateMax = center + halfSize + glm::vec2(padding);

	// Check room bounds (candidate must be fully inside the room)
	if (candidateMin.x < roomMin.x || candidateMin.y < roomMin.y ||
		candidateMax.x > roomMax.x || candidateMax.y > roomMax.y)
	{
		return false;
	}

	// Check overlap with every existing footprint
	for (const auto& fp : placed)
	{
		// AABB vs AABB overlap test (no padding on existing items — padding is on the candidate)
		if (candidateMin.x < fp.max.x && candidateMax.x > fp.min.x &&
			candidateMin.y < fp.max.y && candidateMax.y > fp.min.y)
		{
			return false; // Overlap detected
		}
	}

	return true;
}

void RoomOccupancy::Register(glm::vec2 center, glm::vec2 halfSize)
{
	PlacedFootprint fp;
	fp.min = center - halfSize;
	fp.max = center + halfSize;
	placed.push_back(fp);
}

void RoomOccupancy::BlockDoor(glm::vec3 doorPosition, float doorWidth, bool runsAlongX, float wallThickness)
{
	float eps = wallThickness + 0.1f;
	if (doorPosition.x >= roomMin.x - eps && doorPosition.x <= roomMax.x + eps &&
		doorPosition.z >= roomMin.y - eps && doorPosition.z <= roomMax.y + eps)
	{
		float clearanceDepth = 1.5f; // Deep walkway clearance
		float clearanceWidthHalf = (doorWidth * 0.5f) + 0.6f; // 60cm extra padding on each side of the door

		glm::vec2 doorCenter(doorPosition.x, doorPosition.z);
		glm::vec2 halfSize;
		if (runsAlongX)
		{
			halfSize = glm::vec2(clearanceWidthHalf, clearanceDepth);
		}
		else
		{
			halfSize = glm::vec2(clearanceDepth, clearanceWidthHalf);
		}
		Register(doorCenter, halfSize);
	}
}

bool RoomOccupancy::TryPlaceAlongWall(int wall, glm::vec2 halfSize, float wallOffset,
	glm::vec2& outCenter, float padding, float stepSize, bool preferCorner) const
{
	// Determine the axis along the wall and the fixed perpendicular position
	// wall 0 = -X: scan along Z, fixed X = roomMin.x + wallOffset
	// wall 1 = +X: scan along Z, fixed X = roomMax.x - wallOffset
	// wall 2 = -Z: scan along X, fixed Z(y) = roomMin.y + wallOffset
	// wall 3 = +Z: scan along X, fixed Z(y) = roomMax.y - wallOffset

	float fixedCoord;
	float scanMin, scanMax;
	float scanHalf; // half-extent along the scan axis
	bool scanAlongX;

	switch (wall)
	{
	case 0: // -X wall
		fixedCoord = roomMin.x + wallOffset;
		scanMin = roomMin.y + halfSize.y + padding;
		scanMax = roomMax.y - halfSize.y - padding;
		scanHalf = halfSize.y;
		scanAlongX = false;
		break;
	case 1: // +X wall
		fixedCoord = roomMax.x - wallOffset;
		scanMin = roomMin.y + halfSize.y + padding;
		scanMax = roomMax.y - halfSize.y - padding;
		scanHalf = halfSize.y;
		scanAlongX = false;
		break;
	case 2: // -Z wall
		fixedCoord = roomMin.y + wallOffset;
		scanMin = roomMin.x + halfSize.x + padding;
		scanMax = roomMax.x - halfSize.x - padding;
		scanHalf = halfSize.x;
		scanAlongX = true;
		break;
	case 3: // +Z wall
		fixedCoord = roomMax.y - wallOffset;
		scanMin = roomMin.x + halfSize.x + padding;
		scanMax = roomMax.x - halfSize.x - padding;
		scanHalf = halfSize.x;
		scanAlongX = true;
		break;
	default:
		return false;
	}

	if (scanMin > scanMax) return false; // Room too small

	if (preferCorner)
	{
		// Force the algorithm to test the maximum edge (bottom-right corner) first!
		for (float s = scanMax; s >= scanMin; s -= stepSize)
		{
			glm::vec2 candidate = scanAlongX ? glm::vec2(s, fixedCoord) : glm::vec2(fixedCoord, s);
			if (CanPlace(candidate, halfSize, padding))
			{
				outCenter = candidate;
				return true;
			}
		}
		// Fallback to testing the minimum edge (top-left corner)
		for (float s = scanMin; s <= scanMax; s += stepSize)
		{
			glm::vec2 candidate = scanAlongX ? glm::vec2(s, fixedCoord) : glm::vec2(fixedCoord, s);
			if (CanPlace(candidate, halfSize, padding))
			{
				outCenter = candidate;
				return true;
			}
		}
		return false;
	}

	// Try center first (most natural looking)
	float centerScan = (scanMin + scanMax) * 0.5f;
	glm::vec2 candidate = scanAlongX
		? glm::vec2(centerScan, fixedCoord)
		: glm::vec2(fixedCoord, centerScan);

	if (CanPlace(candidate, halfSize, padding))
	{
		outCenter = candidate;
		return true;
	}

	// Scan outward from center in both directions
	for (float offset = stepSize; offset <= (scanMax - scanMin) * 0.5f + stepSize; offset += stepSize)
	{
		// Try left/down of center
		float posA = centerScan - offset;
		if (posA >= scanMin)
		{
			candidate = scanAlongX
				? glm::vec2(posA, fixedCoord)
				: glm::vec2(fixedCoord, posA);
			if (CanPlace(candidate, halfSize, padding))
			{
				outCenter = candidate;
				return true;
			}
		}

		// Try right/up of center
		float posB = centerScan + offset;
		if (posB <= scanMax)
		{
			candidate = scanAlongX
				? glm::vec2(posB, fixedCoord)
				: glm::vec2(fixedCoord, posB);
			if (CanPlace(candidate, halfSize, padding))
			{
				outCenter = candidate;
				return true;
			}
		}
	}

	return false; // No valid position found
}

bool RoomOccupancy::TryPlaceInCorner(glm::vec2 halfSize, glm::vec2& outCenter,
	float cornerOffset, float padding) const
{
	// Try all 4 corners: (-X,-Z), (+X,-Z), (-X,+Z), (+X,+Z)
	glm::vec2 corners[4] = {
		glm::vec2(roomMin.x + cornerOffset, roomMin.y + cornerOffset),
		glm::vec2(roomMax.x - cornerOffset, roomMin.y + cornerOffset),
		glm::vec2(roomMin.x + cornerOffset, roomMax.y - cornerOffset),
		glm::vec2(roomMax.x - cornerOffset, roomMax.y - cornerOffset),
	};

	for (int i = 0; i < 4; i++)
	{
		if (CanPlace(corners[i], halfSize, padding))
		{
			outCenter = corners[i];
			return true;
		}
	}
	return false;
}
