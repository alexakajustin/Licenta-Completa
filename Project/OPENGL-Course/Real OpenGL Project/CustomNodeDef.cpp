#include "CustomNodeDef.h"
#include <algorithm>

// =====================================================================
//  Type string conversions
// =====================================================================

std::string CustomNodeDef::PinTypeToString(PinDataType type)
{
	switch (type)
	{
	case PinDataType::Mesh:          return "Mesh";
	case PinDataType::TransformList: return "TransformList";
	case PinDataType::Float:         return "Float";
	case PinDataType::Int:           return "Int";
	case PinDataType::Vec3:          return "Vec3";
	case PinDataType::Vec2:          return "Vec2";
	case PinDataType::Bool:          return "Bool";
	default:                         return "None";
	}
}

PinDataType CustomNodeDef::StringToPinType(const std::string& str)
{
	if (str == "Mesh")          return PinDataType::Mesh;
	if (str == "TransformList") return PinDataType::TransformList;
	if (str == "Float")         return PinDataType::Float;
	if (str == "Int")           return PinDataType::Int;
	if (str == "Vec3")          return PinDataType::Vec3;
	if (str == "Vec2")          return PinDataType::Vec2;
	if (str == "Bool")          return PinDataType::Bool;
	return PinDataType::None;
}

std::string CustomNodeDef::ParamTypeToString(ParamType type)
{
	switch (type)
	{
	case ParamType::Float: return "Float";
	case ParamType::Int:   return "Int";
	case ParamType::Vec2:  return "Vec2";
	case ParamType::Vec3:  return "Vec3";
	case ParamType::Bool:  return "Bool";
	case ParamType::Enum:  return "Enum";
	default:               return "Float";
	}
}

ParamType CustomNodeDef::StringToParamType(const std::string& str)
{
	if (str == "Float") return ParamType::Float;
	if (str == "Int")   return ParamType::Int;
	if (str == "Vec2")  return ParamType::Vec2;
	if (str == "Vec3")  return ParamType::Vec3;
	if (str == "Bool")  return ParamType::Bool;
	if (str == "Enum")  return ParamType::Enum;
	return ParamType::Float;
}

// =====================================================================
//  JSON Helper — simple hand-rolled writer (no external dependencies)
// =====================================================================

static std::string EscapeJsonString(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s)
	{
		switch (c)
		{
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:   out += c;      break;
		}
	}
	return out;
}

static void WriteIndent(std::ofstream& f, int depth)
{
	for (int i = 0; i < depth; i++) f << "  ";
}

static void WriteParamValue(std::ofstream& f, const ParamValue& val, int depth)
{
	f << "{\n";
	WriteIndent(f, depth + 1); f << "\"type\": \"" << CustomNodeDef::ParamTypeToString(val.type) << "\",\n";
	switch (val.type)
	{
	case ParamType::Float:
		WriteIndent(f, depth + 1); f << "\"value\": " << val.floatVal << "\n";
		break;
	case ParamType::Int:
		WriteIndent(f, depth + 1); f << "\"value\": " << val.intVal << "\n";
		break;
	case ParamType::Vec2:
		WriteIndent(f, depth + 1); f << "\"value\": [" << val.vec2Val.x << ", " << val.vec2Val.y << "]\n";
		break;
	case ParamType::Vec3:
		WriteIndent(f, depth + 1); f << "\"value\": [" << val.vec3Val.x << ", " << val.vec3Val.y << ", " << val.vec3Val.z << "]\n";
		break;
	case ParamType::Bool:
		WriteIndent(f, depth + 1); f << "\"value\": " << (val.boolVal ? "true" : "false") << "\n";
		break;
	case ParamType::Enum:
		WriteIndent(f, depth + 1); f << "\"value\": " << val.enumVal << "\n";
		break;
	}
	WriteIndent(f, depth); f << "}";
}

// =====================================================================
//  SaveToFile
// =====================================================================

bool CustomNodeDef::SaveToFile(const std::string& path) const
{
	// Create parent directory if it doesn't exist
	std::filesystem::path dirPath = std::filesystem::path(path).parent_path();
	if (!dirPath.empty())
	{
		std::error_code ec;
		std::filesystem::create_directories(dirPath, ec);
		if (ec)
		{
			printf("[CustomNodeDef] Failed to create directory '%s': %s\n", dirPath.string().c_str(), ec.message().c_str());
			return false;
		}
	}

	std::ofstream f(path);
	if (!f.is_open())
	{
		printf("[CustomNodeDef] Failed to open file for writing: %s\n", path.c_str());
		return false;
	}

	f << "{\n";
	f << "  \"name\": \"" << EscapeJsonString(name) << "\",\n";
	f << "  \"category\": \"" << EscapeJsonString(category) << "\",\n";

	// Inputs
	f << "  \"inputs\": [\n";
	for (int i = 0; i < (int)inputDefs.size(); i++)
	{
		f << "    { \"name\": \"" << EscapeJsonString(inputDefs[i].name) << "\", \"type\": \""
		  << PinTypeToString(inputDefs[i].type) << "\" }";
		if (i + 1 < (int)inputDefs.size()) f << ",";
		f << "\n";
	}
	f << "  ],\n";

	// Outputs
	f << "  \"outputs\": [\n";
	for (int i = 0; i < (int)outputDefs.size(); i++)
	{
		f << "    { \"name\": \"" << EscapeJsonString(outputDefs[i].name) << "\", \"type\": \""
		  << PinTypeToString(outputDefs[i].type) << "\" }";
		if (i + 1 < (int)outputDefs.size()) f << ",";
		f << "\n";
	}
	f << "  ],\n";

	// Operations
	f << "  \"operations\": [\n";
	for (int i = 0; i < (int)operations.size(); i++)
	{
		const OperationSlot& slot = operations[i];
		f << "    {\n";
		f << "      \"type\": \"" << EscapeJsonString(slot.operationName) << "\"";

		if (!slot.paramOverrides.empty())
		{
			f << ",\n";
			f << "      \"params\": {\n";
			int paramIdx = 0;
			for (const auto& pair : slot.paramOverrides)
			{
				f << "        \"" << EscapeJsonString(pair.first) << "\": ";
				WriteParamValue(f, pair.second, 4);
				if (paramIdx + 1 < (int)slot.paramOverrides.size()) f << ",";
				f << "\n";
				paramIdx++;
			}
			f << "      }\n";
		}
		else
		{
			f << "\n";
		}

		f << "    }";
		if (i + 1 < (int)operations.size()) f << ",";
		f << "\n";
	}
	f << "  ]\n";
	f << "}\n";

	f.close();
	printf("[CustomNodeDef] Saved '%s' to %s\n", name.c_str(), path.c_str());
	return true;
}

// =====================================================================
//  JSON Parsing Helpers (minimal hand-rolled parser)
// =====================================================================

// Minimal JSON tokenizer — just enough for our format
static void SkipWhitespace(const std::string& json, size_t& pos)
{
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
		pos++;
}

static std::string ParseJsonString(const std::string& json, size_t& pos)
{
	SkipWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '"') return "";
	pos++; // skip opening "

	std::string result;
	while (pos < json.size() && json[pos] != '"')
	{
		if (json[pos] == '\\' && pos + 1 < json.size())
		{
			pos++;
			switch (json[pos])
			{
			case '"': result += '"'; break;
			case '\\': result += '\\'; break;
			case 'n': result += '\n'; break;
			case 'r': result += '\r'; break;
			case 't': result += '\t'; break;
			default: result += json[pos]; break;
			}
		}
		else
		{
			result += json[pos];
		}
		pos++;
	}
	if (pos < json.size()) pos++; // skip closing "
	return result;
}

static double ParseJsonNumber(const std::string& json, size_t& pos)
{
	SkipWhitespace(json, pos);
	size_t start = pos;
	if (pos < json.size() && json[pos] == '-') pos++;
	while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+' || json[pos] == '-'))
		pos++;
	if (pos == start) return 0.0;
	return std::stod(json.substr(start, pos - start));
}

static bool ParseJsonBool(const std::string& json, size_t& pos)
{
	SkipWhitespace(json, pos);
	if (json.substr(pos, 4) == "true") { pos += 4; return true; }
	if (json.substr(pos, 5) == "false") { pos += 5; return false; }
	return false;
}

static void SkipJsonValue(const std::string& json, size_t& pos);

static void SkipJsonObject(const std::string& json, size_t& pos)
{
	SkipWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '{') return;
	pos++;
	int depth = 1;
	while (pos < json.size() && depth > 0)
	{
		if (json[pos] == '{') depth++;
		else if (json[pos] == '}') depth--;
		if (depth > 0) pos++;
	}
	if (pos < json.size()) pos++;
}

static void SkipJsonArray(const std::string& json, size_t& pos)
{
	SkipWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '[') return;
	pos++;
	int depth = 1;
	while (pos < json.size() && depth > 0)
	{
		if (json[pos] == '[') depth++;
		else if (json[pos] == ']') depth--;
		if (depth > 0) pos++;
	}
	if (pos < json.size()) pos++;
}

static void SkipJsonValue(const std::string& json, size_t& pos)
{
	SkipWhitespace(json, pos);
	if (pos >= json.size()) return;
	if (json[pos] == '"') ParseJsonString(json, pos);
	else if (json[pos] == '{') SkipJsonObject(json, pos);
	else if (json[pos] == '[') SkipJsonArray(json, pos);
	else if (json[pos] == 't' || json[pos] == 'f') ParseJsonBool(json, pos);
	else if (json[pos] == 'n') { pos += 4; } // null
	else ParseJsonNumber(json, pos);
}

// Parse a ParamValue from JSON at the current position (expects { ... })
static ParamValue ParseParamValue(const std::string& json, size_t& pos)
{
	ParamValue val;
	SkipWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '{') return val;
	pos++; // skip {

	std::string paramTypeName;

	while (pos < json.size() && json[pos] != '}')
	{
		SkipWhitespace(json, pos);
		if (json[pos] == '}') break;
		if (json[pos] == ',') { pos++; continue; }

		std::string key = ParseJsonString(json, pos);
		SkipWhitespace(json, pos);
		if (pos < json.size() && json[pos] == ':') pos++;
		SkipWhitespace(json, pos);

		if (key == "type")
		{
			paramTypeName = ParseJsonString(json, pos);
			val.type = CustomNodeDef::StringToParamType(paramTypeName);
		}
		else if (key == "value")
		{
			switch (val.type)
			{
			case ParamType::Float:
				val.floatVal = (float)ParseJsonNumber(json, pos);
				break;
			case ParamType::Int:
				val.intVal = (int)ParseJsonNumber(json, pos);
				break;
			case ParamType::Bool:
				val.boolVal = ParseJsonBool(json, pos);
				break;
			case ParamType::Enum:
				val.enumVal = (int)ParseJsonNumber(json, pos);
				break;
			case ParamType::Vec2:
			{
				SkipWhitespace(json, pos);
				if (json[pos] == '[') pos++;
				val.vec2Val.x = (float)ParseJsonNumber(json, pos);
				SkipWhitespace(json, pos); if (json[pos] == ',') pos++;
				val.vec2Val.y = (float)ParseJsonNumber(json, pos);
				SkipWhitespace(json, pos); if (json[pos] == ']') pos++;
				break;
			}
			case ParamType::Vec3:
			{
				SkipWhitespace(json, pos);
				if (json[pos] == '[') pos++;
				val.vec3Val.x = (float)ParseJsonNumber(json, pos);
				SkipWhitespace(json, pos); if (json[pos] == ',') pos++;
				val.vec3Val.y = (float)ParseJsonNumber(json, pos);
				SkipWhitespace(json, pos); if (json[pos] == ',') pos++;
				val.vec3Val.z = (float)ParseJsonNumber(json, pos);
				SkipWhitespace(json, pos); if (json[pos] == ']') pos++;
				break;
			}
			}
		}
		else
		{
			SkipJsonValue(json, pos);
		}
	}

	if (pos < json.size() && json[pos] == '}') pos++;
	return val;
}

// =====================================================================
//  LoadFromFile
// =====================================================================

bool CustomNodeDef::LoadFromFile(const std::string& path, CustomNodeDef& outDef)
{
	std::ifstream f(path);
	if (!f.is_open())
	{
		printf("[CustomNodeDef] Failed to open file: %s\n", path.c_str());
		return false;
	}

	std::stringstream ss;
	ss << f.rdbuf();
	std::string json = ss.str();
	f.close();

	outDef = CustomNodeDef(); // Reset to defaults
	size_t pos = 0;

	SkipWhitespace(json, pos);
	if (pos >= json.size() || json[pos] != '{')
	{
		printf("[CustomNodeDef] Invalid JSON (expected '{') in %s\n", path.c_str());
		return false;
	}
	pos++;

	while (pos < json.size() && json[pos] != '}')
	{
		SkipWhitespace(json, pos);
		if (json[pos] == '}') break;
		if (json[pos] == ',') { pos++; continue; }

		std::string key = ParseJsonString(json, pos);
		SkipWhitespace(json, pos);
		if (pos < json.size() && json[pos] == ':') pos++;
		SkipWhitespace(json, pos);

		if (key == "name")
		{
			outDef.name = ParseJsonString(json, pos);
		}
		else if (key == "category")
		{
			outDef.category = ParseJsonString(json, pos);
		}
		else if (key == "inputs")
		{
			if (json[pos] == '[') pos++;
			while (pos < json.size() && json[pos] != ']')
			{
				SkipWhitespace(json, pos);
				if (json[pos] == ']') break;
				if (json[pos] == ',') { pos++; continue; }
				if (json[pos] == '{')
				{
					pos++;
					PinDefinition pin;
					while (pos < json.size() && json[pos] != '}')
					{
						SkipWhitespace(json, pos);
						if (json[pos] == '}') break;
						if (json[pos] == ',') { pos++; continue; }
						std::string k = ParseJsonString(json, pos);
						SkipWhitespace(json, pos); if (json[pos] == ':') pos++;
						SkipWhitespace(json, pos);
						if (k == "name") pin.name = ParseJsonString(json, pos);
						else if (k == "type") pin.type = StringToPinType(ParseJsonString(json, pos));
						else SkipJsonValue(json, pos);
					}
					if (pos < json.size() && json[pos] == '}') pos++;
					outDef.inputDefs.push_back(pin);
				}
			}
			if (pos < json.size() && json[pos] == ']') pos++;
		}
		else if (key == "outputs")
		{
			if (json[pos] == '[') pos++;
			while (pos < json.size() && json[pos] != ']')
			{
				SkipWhitespace(json, pos);
				if (json[pos] == ']') break;
				if (json[pos] == ',') { pos++; continue; }
				if (json[pos] == '{')
				{
					pos++;
					PinDefinition pin;
					while (pos < json.size() && json[pos] != '}')
					{
						SkipWhitespace(json, pos);
						if (json[pos] == '}') break;
						if (json[pos] == ',') { pos++; continue; }
						std::string k = ParseJsonString(json, pos);
						SkipWhitespace(json, pos); if (json[pos] == ':') pos++;
						SkipWhitespace(json, pos);
						if (k == "name") pin.name = ParseJsonString(json, pos);
						else if (k == "type") pin.type = StringToPinType(ParseJsonString(json, pos));
						else SkipJsonValue(json, pos);
					}
					if (pos < json.size() && json[pos] == '}') pos++;
					outDef.outputDefs.push_back(pin);
				}
			}
			if (pos < json.size() && json[pos] == ']') pos++;
		}
		else if (key == "operations")
		{
			if (json[pos] == '[') pos++;
			while (pos < json.size() && json[pos] != ']')
			{
				SkipWhitespace(json, pos);
				if (json[pos] == ']') break;
				if (json[pos] == ',') { pos++; continue; }
				if (json[pos] == '{')
				{
					pos++;
					OperationSlot slot;
					while (pos < json.size() && json[pos] != '}')
					{
						SkipWhitespace(json, pos);
						if (json[pos] == '}') break;
						if (json[pos] == ',') { pos++; continue; }
						std::string k = ParseJsonString(json, pos);
						SkipWhitespace(json, pos); if (json[pos] == ':') pos++;
						SkipWhitespace(json, pos);
						if (k == "type")
						{
							slot.operationName = ParseJsonString(json, pos);
						}
						else if (k == "params")
						{
							if (json[pos] == '{') pos++;
							while (pos < json.size() && json[pos] != '}')
							{
								SkipWhitespace(json, pos);
								if (json[pos] == '}') break;
								if (json[pos] == ',') { pos++; continue; }
								std::string paramName = ParseJsonString(json, pos);
								SkipWhitespace(json, pos); if (json[pos] == ':') pos++;
								SkipWhitespace(json, pos);
								slot.paramOverrides[paramName] = ParseParamValue(json, pos);
							}
							if (pos < json.size() && json[pos] == '}') pos++;
						}
						else
						{
							SkipJsonValue(json, pos);
						}
					}
					if (pos < json.size() && json[pos] == '}') pos++;
					outDef.operations.push_back(slot);
				}
			}
			if (pos < json.size() && json[pos] == ']') pos++;
		}
		else
		{
			SkipJsonValue(json, pos);
		}
	}

	printf("[CustomNodeDef] Loaded '%s' from %s (%d inputs, %d outputs, %d operations)\n",
		outDef.name.c_str(), path.c_str(),
		(int)outDef.inputDefs.size(), (int)outDef.outputDefs.size(), (int)outDef.operations.size());
	return true;
}
