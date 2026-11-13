#pragma once

#include <string>

class GameObject;
class Component;
class LightObject;
struct ImGuiPayload;

/**
 * @brief Drag & drop of scene things into Lua code.
 *
 * Anything that can be referenced from a script (a GameObject, its Transform, Material, a texture
 * layer, any component, another script) becomes a drag source with a "SCRIPT_REF" payload: the
 * Lua expression that reaches it plus a suggested variable name. The Script Editor turns a drop
 * into `local <variable> = <expression>` on the line under the mouse.
 *
 * Call one of the *DragSource functions right after the ImGui item that should be draggable
 * (typically a CollapsingHeader).
 */
namespace ScriptReference
{
	inline constexpr const char* PayloadType = "SCRIPT_REF";

	struct Payload
	{
		char expression[512] = {};  // Scene.Find("Cube"):GetLayer(0)
		char variable[64] = {};     // cubeLayer0
		char label[128] = {};       // Cube > Layer 0
		char objectName[128] = {};  // Cube (lets the editor use 'gameObject' for the script's own object)
	};

	// Lua expression reaching an object: Scene.Find("Name")
	std::string ObjectExpression(const GameObject* go);
	// Lua expression reaching a light: Scene.FindLight("Sun")
	std::string LightExpression(const LightObject* light);

	// Payload for any expression (property paths such as Scene.Find("Cube").transform.rotation.x)
	Payload Make(const std::string& expression, const std::string& variable, const std::string& label, const std::string& objectName = "");
	// camelCase Lua identifier from free text ("Main Cube" + "Transform" -> "mainCubeTransform")
	std::string MakeVariableName(const std::string& base, const std::string& suffix = "");

	// Fills expression / variable / label for a component of 'owner'
	bool DescribeComponent(const GameObject* owner, const Component* component, Payload& out);

	// Lua expression loading an asset file, chosen by extension (Assets.Texture(...), Script.Require(...))
	// Returns false for files a script cannot load.
	bool DescribeAsset(const std::string& path, Payload& out);

	void DragSource(const Payload& payload);
	void ObjectDragSource(const GameObject* go);
	void TransformDragSource(const GameObject* go);
	void MaterialDragSource(const GameObject* go);
	void LayerDragSource(const GameObject* go, int layerIndex);
	void ComponentDragSource(const Component* component);
	void LightDragSource(const LightObject* light);

	// Payload from any accepted drag & drop type ("SCRIPT_REF", "ASSET_PATH", "MATERIAL_PATH",
	// "SCRIPT_PATH"). Returns false if the active payload is not something code can reference.
	bool FromImGuiPayload(const ImGuiPayload* payload, Payload& out);
}
