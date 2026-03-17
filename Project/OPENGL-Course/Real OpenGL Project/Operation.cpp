#include "Operation.h"
#include "imgui.h"

void Operation::RenderUI()
{
	auto defs = GetParamDefs();
	for (const auto& def : defs)
	{
		auto it = params.find(def.name);
		if (it == params.end()) continue;

		ParamValue& val = it->second;

		switch (def.type)
		{
		case ParamType::Float:
			ImGui::SliderFloat(def.name.c_str(), &val.floatVal, def.minFloat, def.maxFloat);
			break;

		case ParamType::Int:
			ImGui::SliderInt(def.name.c_str(), &val.intVal, def.minInt, def.maxInt);
			break;

		case ParamType::Vec2:
			ImGui::DragFloat2(def.name.c_str(), &val.vec2Val.x, 0.01f);
			break;

		case ParamType::Vec3:
			ImGui::DragFloat3(def.name.c_str(), &val.vec3Val.x, 0.01f);
			break;

		case ParamType::Bool:
			ImGui::Checkbox(def.name.c_str(), &val.boolVal);
			break;

		case ParamType::Enum:
		{
			const char* preview = (val.enumVal >= 0 && val.enumVal < (int)def.enumOptions.size())
				? def.enumOptions[val.enumVal].c_str()
				: "???";
			if (ImGui::BeginCombo(def.name.c_str(), preview))
			{
				for (int i = 0; i < (int)def.enumOptions.size(); i++)
				{
					bool isSelected = (val.enumVal == i);
					if (ImGui::Selectable(def.enumOptions[i].c_str(), isSelected))
						val.enumVal = i;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			break;
		}
		}
	}
}
