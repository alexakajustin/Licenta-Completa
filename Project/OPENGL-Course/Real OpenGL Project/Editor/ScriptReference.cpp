#include "Editor/ScriptReference.h"

#include "imgui.h"
#include "Scene/GameObject.h"
#include "Scene/Component.h"
#include "Scene/LuaScriptComponent.h"
#include "Scene/RigidBody.h"
#include "Scene/BoxCollider.h"
#include "Scene/CapsuleCollider.h"
#include "Scene/MeshCollider.h"
#include "Scene/Player.h"
#include "Lighting/LightObject.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace ScriptReference
{
	namespace
	{
		void Copy(char* dst, size_t size, const std::string& src)
		{
			strncpy_s(dst, size, src.c_str(), _TRUNCATE);
		}

		std::string LuaString(const std::string& s)
		{
			std::string out = "\"";
			for (char c : s)
			{
				if (c == '"' || c == '\\') out.push_back('\\');
				out.push_back(c);
			}
			return out + "\"";
		}

		std::string NormalizePath(std::string p)
		{
			for (char& c : p) if (c == '\\') c = '/';
			// Keep project-relative paths ("Assets/...") even if an absolute one was given
			std::string lower = p;
			for (char& c : lower) c = (char)std::tolower((unsigned char)c);
			size_t pos = lower.find("assets/");
			if (pos != std::string::npos && pos > 0) p = p.substr(pos);
			return p;
		}

		const std::unordered_set<std::string>& LuaKeywords()
		{
			static const std::unordered_set<std::string> k = {
				"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
				"local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" };
			return k;
		}
	}

	std::string ObjectExpression(const GameObject* go)
	{
		return go ? "Scene.Find(" + LuaString(go->GetName()) + ")" : "nil";
	}

	std::string LightExpression(const LightObject* light)
	{
		return light ? "Scene.FindLight(" + LuaString(light->GetName()) + ")" : "nil";
	}

	Payload Make(const std::string& expression, const std::string& variable, const std::string& label, const std::string& objectName)
	{
		Payload p;
		Copy(p.expression, sizeof(p.expression), expression);
		Copy(p.variable, sizeof(p.variable), variable);
		Copy(p.label, sizeof(p.label), label);
		Copy(p.objectName, sizeof(p.objectName), objectName);
		return p;
	}

	void LightDragSource(const LightObject* light)
	{
		if (!light) return;
		DragSource(Make(LightExpression(light), MakeVariableName(light->GetName(), "Light"), light->GetName() + " (light)"));
	}

	std::string MakeVariableName(const std::string& base, const std::string& suffix)
	{
		std::string out;
		bool upperNext = false;
		auto append = [&](const std::string& text, bool capitalizeFirst) {
			bool first = true;
			for (char c : text)
			{
				if (std::isalnum((unsigned char)c))
				{
					if (out.empty()) out.push_back((char)std::tolower((unsigned char)c));
					else if (upperNext || (first && capitalizeFirst)) out.push_back((char)std::toupper((unsigned char)c));
					else out.push_back(c);
					upperNext = false;
					first = false;
				}
				else if (!out.empty())
				{
					upperNext = true;
				}
			}
		};
		append(base, false);
		upperNext = false;
		append(suffix, true);
		if (out.empty()) out = "ref";
		if (std::isdigit((unsigned char)out[0])) out = "obj" + out;
		if (LuaKeywords().count(out)) out += "Ref";
		return out;
	}

	bool DescribeComponent(const GameObject* owner, const Component* component, Payload& out)
	{
		if (!owner || !component) return false;
		std::string obj = ObjectExpression(owner);
		std::string name = owner->GetName();
		std::string expr, var, label;

		if (auto* script = dynamic_cast<const LuaScriptComponent*>(component))
		{
			std::string scriptName = script->GetScriptName();
			if (scriptName.empty()) return false;
			expr = obj + ":GetScript(" + LuaString(scriptName) + ")";
			var = MakeVariableName(name, scriptName);
			label = name + " > " + scriptName + ".lua";
		}
		else
		{
			const char* getter = nullptr;
			if (dynamic_cast<const RigidBody*>(component)) getter = "GetRigidBody";
			else if (dynamic_cast<const BoxCollider*>(component)) getter = "GetBoxCollider";
			else if (dynamic_cast<const CapsuleCollider*>(component)) getter = "GetCapsuleCollider";
			else if (dynamic_cast<const MeshCollider*>(component)) getter = "GetMeshCollider";
			else if (dynamic_cast<const Player*>(component)) getter = "GetPlayer";

			std::string type = component->GetName();
			expr = getter ? obj + ":" + getter + "()" : obj + ":GetComponent(" + LuaString(type) + ")";
			var = MakeVariableName(name, type);
			label = name + " > " + type;
		}

		Copy(out.expression, sizeof(out.expression), expr);
		Copy(out.variable, sizeof(out.variable), var);
		Copy(out.label, sizeof(out.label), label);
		Copy(out.objectName, sizeof(out.objectName), name);
		return true;
	}

	bool DescribeAsset(const std::string& rawPath, Payload& out)
	{
		std::string path = NormalizePath(rawPath);
		std::filesystem::path p(path);
		std::string ext = p.extension().string();
		for (char& c : ext) c = (char)std::tolower((unsigned char)c);
		std::string stem = p.stem().string();

		std::string expr, suffix;
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
		{ expr = "Assets.Texture(" + LuaString(path) + ")"; suffix = "Texture"; }
		else if (ext == ".mat")
		{ expr = "Assets.Material(" + LuaString(path) + ")"; suffix = "Material"; }
		else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae")
		{ expr = "Assets.Model(" + LuaString(path) + ")"; suffix = "Model"; }
		else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
		{ expr = "Assets.Audio(" + LuaString(path) + ")"; suffix = "Sound"; }
		else if (ext == ".lua")
		{ expr = "Script.Require(" + LuaString(stem) + ")"; suffix = ""; }
		else
			return false;

		Copy(out.expression, sizeof(out.expression), expr);
		Copy(out.variable, sizeof(out.variable), MakeVariableName(stem, suffix));
		Copy(out.label, sizeof(out.label), p.filename().string());
		out.objectName[0] = '\0';
		return true;
	}

	void DragSource(const Payload& payload)
	{
		if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return;
		ImGui::SetDragDropPayload(PayloadType, &payload, sizeof(Payload));
		ImGui::TextUnformatted(payload.label);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.78f, 1.0f, 1.0f));
		ImGui::Text("local %s = %s", payload.variable, payload.expression);
		ImGui::PopStyleColor();
		ImGui::TextDisabled("Drop into the Script Editor");
		ImGui::EndDragDropSource();
	}

	void ObjectDragSource(const GameObject* go)
	{
		if (!go) return;
		Payload p;
		Copy(p.expression, sizeof(p.expression), ObjectExpression(go));
		Copy(p.variable, sizeof(p.variable), MakeVariableName(go->GetName()));
		Copy(p.label, sizeof(p.label), go->GetName());
		Copy(p.objectName, sizeof(p.objectName), go->GetName());
		DragSource(p);
	}

	void TransformDragSource(const GameObject* go)
	{
		if (!go) return;
		Payload p;
		Copy(p.expression, sizeof(p.expression), ObjectExpression(go) + ".transform");
		Copy(p.variable, sizeof(p.variable), MakeVariableName(go->GetName(), "Transform"));
		Copy(p.label, sizeof(p.label), go->GetName() + " > Transform");
		Copy(p.objectName, sizeof(p.objectName), go->GetName());
		DragSource(p);
	}

	void MaterialDragSource(const GameObject* go)
	{
		if (!go) return;
		Payload p;
		Copy(p.expression, sizeof(p.expression), ObjectExpression(go) + ".material");
		Copy(p.variable, sizeof(p.variable), MakeVariableName(go->GetName(), "Material"));
		Copy(p.label, sizeof(p.label), go->GetName() + " > Material");
		Copy(p.objectName, sizeof(p.objectName), go->GetName());
		DragSource(p);
	}

	void LayerDragSource(const GameObject* go, int layerIndex)
	{
		if (!go) return;
		Payload p;
		Copy(p.expression, sizeof(p.expression), ObjectExpression(go) + ":GetLayer(" + std::to_string(layerIndex) + ")");
		Copy(p.variable, sizeof(p.variable), MakeVariableName(go->GetName(), "Layer" + std::to_string(layerIndex)));
		Copy(p.label, sizeof(p.label), go->GetName() + " > Layer " + std::to_string(layerIndex));
		Copy(p.objectName, sizeof(p.objectName), go->GetName());
		DragSource(p);
	}

	void ComponentDragSource(const Component* component)
	{
		if (!component) return;
		Payload p;
		if (DescribeComponent(component->GetGameObject(), component, p)) DragSource(p);
	}

	bool FromImGuiPayload(const ImGuiPayload* payload, Payload& out)
	{
		if (!payload || !payload->Data) return false;
		if (payload->IsDataType(PayloadType) && payload->DataSize == sizeof(Payload))
		{
			out = *static_cast<const Payload*>(payload->Data);
			return true;
		}
		if (payload->IsDataType("ASSET_PATH") || payload->IsDataType("MATERIAL_PATH") || payload->IsDataType("SCRIPT_PATH"))
			return DescribeAsset(static_cast<const char*>(payload->Data), out);
		return false;
	}
}
