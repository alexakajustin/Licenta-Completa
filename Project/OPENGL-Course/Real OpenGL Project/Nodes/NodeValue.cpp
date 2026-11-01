#include "Nodes/NodeValue.h"
#include <cmath>
#include <cstdio>

const char* PinTypeName(PinType t)
{
	switch (t) {
	case PinType::Float:   return "Float";
	case PinType::Int:     return "Int";
	case PinType::Bool:    return "Bool";
	case PinType::Vec2:    return "Vec2";
	case PinType::Vec3:    return "Vec3";
	case PinType::Mesh:    return "Mesh";
	case PinType::Points:  return "Points";
	case PinType::Objects: return "Objects";
	default:               return "None";
	}
}

PinType PinTypeFromName(const std::string& name)
{
	if (name == "Float") return PinType::Float;
	if (name == "Int") return PinType::Int;
	if (name == "Bool") return PinType::Bool;
	if (name == "Vec2") return PinType::Vec2;
	if (name == "Vec3") return PinType::Vec3;
	if (name == "Mesh") return PinType::Mesh;
	if (name == "Points" || name == "TransformList") return PinType::Points;
	if (name == "Objects") return PinType::Objects;
	return PinType::None;
}

bool PinTypesConvertible(PinType from, PinType to)
{
	if (from == PinType::None || to == PinType::None) return false;
	if (from == to) return true;
	return IsScalarPin(from) && IsScalarPin(to);
}

const MeshData& MeshValue::Data() const
{
	static const MeshData empty;
	return data ? *data : empty;
}

MeshData& MeshValue::Edit()
{
	if (!data) data = std::make_shared<MeshData>();
	else if (data.use_count() > 1) data = std::make_shared<MeshData>(*data);
	// Unique and created non-const: safe to write
	return const_cast<MeshData&>(*data);
}

float NodeValue::AsFloat() const
{
	switch (type) {
	case PinType::Float: return f;
	case PinType::Int:   return (float)i;
	case PinType::Bool:  return b ? 1.0f : 0.0f;
	case PinType::Vec2:  return v2.x;
	case PinType::Vec3:  return v3.x;
	default:             return 0.0f;
	}
}

int NodeValue::AsInt() const
{
	switch (type) {
	case PinType::Int:   return i;
	case PinType::Float: return (int)std::lround(f);
	case PinType::Bool:  return b ? 1 : 0;
	case PinType::Vec2:  return (int)std::lround(v2.x);
	case PinType::Vec3:  return (int)std::lround(v3.x);
	default:             return 0;
	}
}

bool NodeValue::AsBool() const
{
	switch (type) {
	case PinType::Bool:  return b;
	case PinType::Int:   return i != 0;
	case PinType::Float: return f != 0.0f;
	case PinType::Vec2:  return v2.x != 0.0f;
	case PinType::Vec3:  return v3.x != 0.0f;
	default:             return false;
	}
}

glm::vec2 NodeValue::AsVec2() const
{
	switch (type) {
	case PinType::Vec2: return v2;
	case PinType::Vec3: return glm::vec2(v3.x, v3.y);
	default:            return glm::vec2(AsFloat());
	}
}

glm::vec3 NodeValue::AsVec3() const
{
	switch (type) {
	case PinType::Vec3: return v3;
	case PinType::Vec2: return glm::vec3(v2, 0.0f);
	default:            return glm::vec3(AsFloat());
	}
}

NodeValue NodeValue::ConvertTo(PinType target) const
{
	if (type == target || !PinTypesConvertible(type, target)) return *this;
	switch (target) {
	case PinType::Float: return Float(AsFloat());
	case PinType::Int:   return Int(AsInt());
	case PinType::Bool:  return Bool(AsBool());
	case PinType::Vec2:  return Vec2(AsVec2());
	case PinType::Vec3:  return Vec3(AsVec3());
	default:             return *this;
	}
}

const TransformList& NodeValue::Points() const
{
	static const TransformList empty;
	return points ? *points : empty;
}

const ObjectList& NodeValue::Objects() const
{
	static const ObjectList empty;
	return objects ? *objects : empty;
}

namespace {
	// 1234567 -> "1 234 567"
	std::string Grouped(size_t n)
	{
		std::string s = std::to_string(n);
		for (int pos = (int)s.size() - 3; pos > 0; pos -= 3) s.insert((size_t)pos, " ");
		return s;
	}
}

std::string NodeValue::Summary() const
{
	char buf[96];
	switch (type) {
	case PinType::Float: snprintf(buf, sizeof(buf), "%.3g", f); return buf;
	case PinType::Int:   return std::to_string(i);
	case PinType::Bool:  return b ? "true" : "false";
	case PinType::Vec2:  snprintf(buf, sizeof(buf), "(%.3g, %.3g)", v2.x, v2.y); return buf;
	case PinType::Vec3:  snprintf(buf, sizeof(buf), "(%.3g, %.3g, %.3g)", v3.x, v3.y, v3.z); return buf;
	case PinType::Mesh:
		if (mesh.Empty()) return "empty mesh";
		return Grouped((size_t)mesh.Data().GetVertexCount()) + " vertices";
	case PinType::Points: {
		size_t n = Points().size();
		return Grouped(n) + (n == 1 ? " point" : " points");
	}
	case PinType::Objects: {
		size_t n = Objects().size();
		if (n == 1) return Objects()[0].name;
		return Grouped(n) + " objects";
	}
	default: return "";
	}
}
