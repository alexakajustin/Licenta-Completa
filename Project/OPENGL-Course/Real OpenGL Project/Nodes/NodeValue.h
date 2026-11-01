#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include "Rendering/MeshData.h"
#include "Rendering/TextureLayer.h"

class GameObject;
class Material;
class Texture;

// =====================================================================
// Data flowing through the node graph
//
// Every pin has a type; a link carries one NodeValue from an output to an input. Heavy payloads
// (meshes, point sets, object lists) are shared, immutable and copy-on-write: fanning an output out
// to ten nodes, or passing a mesh through a node untouched, copies nothing.
// =====================================================================

enum class PinType : int
{
	None = 0,
	Float,
	Int,
	Bool,
	Vec2,
	Vec3,
	Mesh,    // Geometry (MeshValue)
	Points,  // Placements: position, rotation, scale, surface normal (TransformList)
	Objects, // Scene objects: a list, any length (ObjectList)
};

inline bool IsScalarPin(PinType t)
{
	return t == PinType::Float || t == PinType::Int || t == PinType::Bool || t == PinType::Vec2 || t == PinType::Vec3;
}

const char* PinTypeName(PinType t);
PinType PinTypeFromName(const std::string& name); // Also the names older saves used ("TransformList")
// Whether a link may carry a value from one type into the other (scalars convert freely)
bool PinTypesConvertible(PinType from, PinType to);

// How generated geometry looks: travels with the mesh to the object that finally shows it
struct Appearance
{
	Material* material = nullptr;
	Texture* texture = nullptr;
	Texture* normalMap = nullptr;
	std::vector<TextureLayer> layers;
};

// Geometry. Vertices are world-oriented (the source object's rotation and scale are baked in) and
// relative to `origin`: a vertex sits at origin + v in the world. Mesh Output undoes the bake when
// it writes the geometry back to an object.
struct MeshValue
{
	std::shared_ptr<const MeshData> data;
	glm::vec3 origin = glm::vec3(0.0f);
	glm::vec3 rotation = glm::vec3(0.0f); // Source object's rotation (degrees), baked into the vertices
	glm::vec3 scale = glm::vec3(1.0f);    // Source object's scale, baked into the vertices
	std::string sourceObject;             // Scene object the geometry was read from ("" = generated)
	Appearance look;

	bool Empty() const { return !data || data->vertices.empty(); }
	const MeshData& Data() const;
	// Writable geometry: a private copy when it is shared
	MeshData& Edit();
	void Set(MeshData&& mesh) { data = std::make_shared<const MeshData>(std::move(mesh)); }
};

// One scene object in an Objects value
struct ObjectEntry
{
	std::string name;
	float weight = 1.0f;         // Relative chance when one object is picked among several
	std::string label;           // Free tag (e.g. the furniture category it stands for)
	GameObject* object = nullptr; // Resolved for the current execution only
};
using ObjectList = std::vector<ObjectEntry>;

struct NodeValue
{
	PinType type = PinType::None;
	float f = 0.0f;
	int i = 0;
	bool b = false;
	glm::vec2 v2 = glm::vec2(0.0f);
	glm::vec3 v3 = glm::vec3(0.0f);
	MeshValue mesh;
	std::shared_ptr<const TransformList> points;
	std::shared_ptr<const ObjectList> objects;

	static NodeValue Float(float v) { NodeValue r; r.type = PinType::Float; r.f = v; return r; }
	static NodeValue Int(int v) { NodeValue r; r.type = PinType::Int; r.i = v; return r; }
	static NodeValue Bool(bool v) { NodeValue r; r.type = PinType::Bool; r.b = v; return r; }
	static NodeValue Vec2(const glm::vec2& v) { NodeValue r; r.type = PinType::Vec2; r.v2 = v; return r; }
	static NodeValue Vec3(const glm::vec3& v) { NodeValue r; r.type = PinType::Vec3; r.v3 = v; return r; }
	static NodeValue FromMesh(MeshValue m) { NodeValue r; r.type = PinType::Mesh; r.mesh = std::move(m); return r; }
	static NodeValue FromPoints(TransformList&& p) { NodeValue r; r.type = PinType::Points; r.points = std::make_shared<const TransformList>(std::move(p)); return r; }
	static NodeValue FromPoints(std::shared_ptr<const TransformList> p) { NodeValue r; r.type = PinType::Points; r.points = std::move(p); return r; }
	static NodeValue FromObjects(ObjectList&& o) { NodeValue r; r.type = PinType::Objects; r.objects = std::make_shared<const ObjectList>(std::move(o)); return r; }

	bool Valid() const { return type != PinType::None; }

	// Scalars convert into each other (a Float drives an Int parameter, a Bool counts as 0/1...)
	float AsFloat() const;
	int AsInt() const;
	bool AsBool() const;
	glm::vec2 AsVec2() const;
	glm::vec3 AsVec3() const;
	NodeValue ConvertTo(PinType target) const;

	const TransformList& Points() const;   // Empty when there are none
	const ObjectList& Objects() const;     // Empty when there are none

	// "4 000 points", "262 144 vertices", "3 objects", "0.45": shown under the node after a run
	std::string Summary() const;
};
