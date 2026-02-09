#include "SceneManager.h"

SceneManager::SceneManager()
	: selectedObjectIndex(-1), selectedLightIndex(-1), pickingFBO(0), pickingTexture(0), pickingDepth(0),
	  pickWidth(0), pickHeight(0), pickingInitialized(false)
{
}

SceneManager::~SceneManager()
{
	Clear();
	
	// Cleanup picking resources
	if (pickingFBO) glDeleteFramebuffers(1, &pickingFBO);
	if (pickingTexture) glDeleteTextures(1, &pickingTexture);
	if (pickingDepth) glDeleteRenderbuffers(1, &pickingDepth);
}

void SceneManager::AddObject(GameObject* obj)
{
	if (obj)
	{
		objects.push_back(obj);
	}
}

void SceneManager::RemoveObject(const std::string& name)
{
	for (auto it = objects.begin(); it != objects.end(); ++it)
	{
		if ((*it)->GetName() == name)
		{
			delete* it;
			objects.erase(it);
			return;
		}
	}
}

GameObject* SceneManager::FindObject(const std::string& name)
{
	for (auto* obj : objects)
	{
		if (obj->GetName() == name)
		{
			return obj;
		}
	}
	return nullptr;
}

void SceneManager::RenderAll(GLuint uniformModel, GLuint uniformSpecularIntensity, GLuint uniformShininess)
{
	for (auto* obj : objects)
	{
		obj->Render(uniformModel, uniformSpecularIntensity, uniformShininess);
	}
}

void SceneManager::AddLight(LightObject* light)
{
	if (light)
	{
		lights.push_back(light);
	}
}

void SceneManager::InitPicking(int width, int height)
{
	pickWidth = width;
	pickHeight = height;

	// Create framebuffer
	glGenFramebuffers(1, &pickingFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);

	// Create color texture
	glGenTextures(1, &pickingTexture);
	glBindTexture(GL_TEXTURE_2D, pickingTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickingTexture, 0);

	// Create depth buffer
	glGenRenderbuffers(1, &pickingDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, pickingDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickingDepth);

	// Check completeness
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("Picking framebuffer not complete!\n");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Load picking shader
	pickingShader.CreateFromFiles("Shaders/picking.vert", "Shaders/picking.frag");

	pickingInitialized = true;
}

int SceneManager::PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view)
{
	if (!pickingInitialized) return -1;

	// Bind picking framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
	glViewport(0, 0, pickWidth, pickHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	pickingShader.UseShader();

	// Set projection and view
	glUniformMatrix4fv(pickingShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(pickingShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));

	GLuint colorLoc = glGetUniformLocation(pickingShader.GetShaderID(), "pickingColor");
	GLuint modelLoc = pickingShader.GetModelLocation();

	// Render each object with unique color based on index
	for (int i = 0; i < (int)objects.size(); i++)
	{
		// Convert index to RGB color (index + 1 to avoid black background)
		int id = i + 1;
		float r = ((id & 0x0000FF) >> 0) / 255.0f;
		float g = ((id & 0x00FF00) >> 8) / 255.0f;
		float b = ((id & 0xFF0000) >> 16) / 255.0f;

		glUniform3f(colorLoc, r, g, b);

		// Set model matrix
		glm::mat4 modelMatrix = objects[i]->GetTransform().GetModelMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelMatrix));

		// Render geometry only
		if (objects[i]->GetModel())
		{
			objects[i]->GetModel()->RenderModel();
		}
		else if (objects[i]->GetMesh())
		{
			objects[i]->GetMesh()->RenderMesh();
		}
	}

	// Read pixel at mouse position
	glFlush();
	glFinish();

	unsigned char pixel[3];
	int readX = (int)mouseX;
	int readY = pickHeight - (int)mouseY; // Flip Y
	glReadPixels(readX, readY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);

	// Convert color back to index
	int pickedID = pixel[0] + pixel[1] * 256 + pixel[2] * 256 * 256;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (pickedID > 0 && pickedID <= (int)objects.size())
	{
		return pickedID - 1; // Convert back to 0-based index
	}

	return -1;
}

void SceneManager::RenderImGui()
{
	ImGui::Begin("Scene Hierarchy");

	// Object list
	if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)objects.size(); i++)
		{
			ImGui::PushID(i);
			// Only highlight if this object is selected AND no light is selected
			bool isSelected = (selectedObjectIndex == i && selectedLightIndex < 0);
			if (ImGui::Selectable(objects[i]->GetName().c_str(), isSelected))
			{
				selectedObjectIndex = i;
				selectedLightIndex = -1; // Clear light selection
			}
			ImGui::PopID();
		}
	}

	// Light list  
	if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)lights.size(); i++)
		{
			ImGui::PushID(1000 + i);
			
			const char* icon = "";
			switch (lights[i]->GetLightType())
			{
			case LightType::Directional: icon = "[D] "; break;
			case LightType::Point: icon = "[P] "; break;
			case LightType::Spot: icon = "[S] "; break;
			}
			
			std::string label = std::string(icon) + lights[i]->GetName();
			// Only highlight if this light is selected AND no object is selected
			bool isSelected = (selectedLightIndex == i && selectedObjectIndex < 0);
			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				selectedLightIndex = i;
				selectedObjectIndex = -1; // Clear object selection
			}
			ImGui::PopID();
		}
	}

	ImGui::End();

	// Show object inspector ONLY if object selected and NO light selected
	bool showObjectInspector = (selectedObjectIndex >= 0 && selectedLightIndex < 0);

	// Inspector panel for selected object
	if (showObjectInspector && selectedObjectIndex < (int)objects.size())
	{
		GameObject* selected = objects[selectedObjectIndex];
		Transform& transform = selected->GetTransform();

		ImGui::Begin("Inspector");

		// Name display
		ImGui::Text("Name: %s", selected->GetName().c_str());
		ImGui::Separator();

		// Transform editing
		ImGui::Text("Transform");

		glm::vec3* pos = transform.GetPositionPtr();
		glm::vec3* rot = transform.GetRotationPtr();
		glm::vec3* scl = transform.GetScalePtr();

		ImGui::DragFloat3("Position", &pos->x, 0.1f);
		ImGui::DragFloat3("Rotation", &rot->x, 1.0f);
		ImGui::DragFloat3("Scale", &scl->x, 0.01f, 0.01f, 100.0f);

		ImGui::Separator();

		// Component info
		ImGui::Text("Components");
		if (selected->GetModel())
		{
			ImGui::BulletText("Model: Loaded");
		}
		if (selected->GetMesh())
		{
			ImGui::BulletText("Mesh: Loaded");
		}
		if (selected->GetTexture())
		{
			ImGui::BulletText("Texture: Loaded");
		}
		if (selected->GetMaterial())
		{
			ImGui::BulletText("Material: Loaded");
		}

		ImGui::End();
	}

	// Inspector panel for selected light
	bool showLightInspector = (selectedLightIndex >= 0 && selectedObjectIndex < 0);
	if (showLightInspector && selectedLightIndex < (int)lights.size())
	{
		LightObject* light = lights[selectedLightIndex];

		ImGui::Begin("Inspector");

		// Name and type display
		ImGui::Text("Name: %s", light->GetName().c_str());
		const char* typeStr = "";
		switch (light->GetLightType())
		{
		case LightType::Directional: typeStr = "Directional Light"; break;
		case LightType::Point: typeStr = "Point Light"; break;
		case LightType::Spot: typeStr = "Spot Light"; break;
		}
		ImGui::Text("Type: %s", typeStr);
		ImGui::Separator();

		// Light properties
		ImGui::Text("Light Properties");

		glm::vec3* color = light->GetColorPtr();
		ImGui::ColorEdit3("Color", &color->x);

		float* ambient = light->GetAmbientIntensityPtr();
		ImGui::SliderFloat("Ambient Intensity", ambient, 0.0f, 1.0f);

		float* diffuse = light->GetDiffuseIntensityPtr();
		ImGui::SliderFloat("Diffuse Intensity", diffuse, 0.0f, 2.0f);

		ImGui::Separator();

		// Position (for point/spot lights)
		glm::vec3* position = light->GetPositionPtr();
		if (position)
		{
			ImGui::DragFloat3("Position", &position->x, 0.1f);
		}

		// Direction (for directional/spot lights)
		glm::vec3* direction = light->GetDirectionPtr();
		if (direction)
		{
			ImGui::DragFloat3("Direction", &direction->x, 0.01f, -1.0f, 1.0f);
		}

		// Attenuation (for point/spot lights)
		float* constant = light->GetConstantPtr();
		float* linear = light->GetLinearPtr();
		float* exponent = light->GetExponentPtr();

		if (constant && linear && exponent)
		{
			ImGui::Separator();
			ImGui::Text("Attenuation");
			ImGui::SliderFloat("Constant", constant, 0.01f, 2.0f);
			ImGui::SliderFloat("Linear", linear, 0.001f, 0.5f);
			ImGui::SliderFloat("Exponent", exponent, 0.001f, 0.5f);
		}

		ImGui::End();
	}
}

void SceneManager::Clear()
{
	for (auto* obj : objects)
	{
		delete obj;
	}
	objects.clear();
	selectedObjectIndex = -1;
}
