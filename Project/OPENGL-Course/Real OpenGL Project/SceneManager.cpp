#include <functional>
#include "SceneManager.h"
#include "CommonValues.h"
#include "GameObject.h"
#include "Material.h"
#include "Texture.h"
#include "Shader.h"
#include "Application.h"
#include "PrimitiveGenerator.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <iostream>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <unordered_set>
#include "InstancedGroup.h"
#include "Renderer.h"
#include "UndoActions.h"
#include "GraphicsSettings.h"

// =====================================================================
// Constructor / Destructor
// =====================================================================

SceneManager::SceneManager()
	: pickingFBO(0), pickingTexture(0), pickingDepth(0),
	  pickWidth(0), pickHeight(0), pickingInitialized(false),
	  lightIconTexture(nullptr), iconMesh(nullptr), gizmoArrowModel(nullptr), gizmoTorusModel(nullptr)
{
}

SceneManager::~SceneManager()
{
	Clear();
	ClearClipboard();
	
	if (pickingFBO) glDeleteFramebuffers(1, &pickingFBO);
	if (pickingTexture) glDeleteTextures(1, &pickingTexture);
	if (pickingDepth) glDeleteRenderbuffers(1, &pickingDepth);

	if (gizmoArrowModel) { gizmoArrowModel->ClearModel(); delete gizmoArrowModel; }
	if (gizmoTorusModel) { gizmoTorusModel->ClearModel(); delete gizmoTorusModel; }
	if (iconMesh) { iconMesh->Release(); iconMesh = nullptr; }
}

// =====================================================================
// Object & Light Management
// =====================================================================

void SceneManager::AddObject(GameObject* obj)
{
	if (obj) objects.push_back(obj);
}

void SceneManager::RemoveObject(const std::string& name)
{
	for (int i = 0; i < (int)objects.size(); i++)
	{
		if (objects[i]->GetName() == name)
		{
			DeleteGameObject(i);
			return;
		}
	}
}

void SceneManager::DeleteSelectedObjects()
{
	for (auto* group : instancedGroups) {
		if (group) group->DeleteSelectedInstances();
	}

	if (selectedObjectIndices.empty()) return;

	// Snapshot for undo: capture objects, their indices, and parent info
	std::vector<DeletedObjectEntry> undoEntries;
	std::vector<int> prevSelection = selectedObjectIndices;

	// Collect objects to delete (sorted ascending by index for snapshot)
	std::vector<std::pair<int, GameObject*>> toDelete;
	for (int idx : selectedObjectIndices) {
		if (idx >= 0 && idx < (int)objects.size()) {
			toDelete.push_back({ idx, objects[idx] });
		}
	}

	// Also collect children that will be recursively deleted
	std::vector<GameObject*> allToDelete;
	for (auto& [idx, obj] : toDelete) {
		allToDelete.push_back(obj);
		// Collect children recursively
		std::function<void(GameObject*)> collectChildren = [&](GameObject* parent) {
			for (auto* child : parent->GetChildren()) {
				allToDelete.push_back(child);
				collectChildren(child);
			}
		};
		collectChildren(obj);
	}

	// Build entries for all objects that will be removed (in order of their indices)
	for (auto* obj : allToDelete) {
		auto it = std::find(objects.begin(), objects.end(), obj);
		if (it != objects.end()) {
			int idx = (int)(it - objects.begin());
			// Check if already captured
			bool alreadyCaptured = false;
			for (auto& e : undoEntries) {
				if (e.object == obj) { alreadyCaptured = true; break; }
			}
			if (!alreadyCaptured) {
				undoEntries.push_back({ obj, idx, obj->GetParent(), obj->GetName() });
			}
		}
	}

	// Remove objects from scene WITHOUT freeing memory (undo action owns them)
	// Process in descending index order to preserve indices
	std::sort(undoEntries.begin(), undoEntries.end(),
		[](const DeletedObjectEntry& a, const DeletedObjectEntry& b) {
			return a.originalIndex > b.originalIndex;
		});

	for (auto& entry : undoEntries) {
		auto it = std::find(objects.begin(), objects.end(), entry.object);
		if (it != objects.end()) {
			int idx = (int)(it - objects.begin());
			// Detach from parent without triggering world recalc
			if (entry.object->GetParent()) {
				entry.object->GetParent()->RemoveChild(entry.object);
			}
			// Orphan children that are NOT being deleted
			for (auto* child : entry.object->GetChildren()) {
				bool childBeingDeleted = false;
				for (auto& e : undoEntries) {
					if (e.object == child) { childBeingDeleted = true; break; }
				}
				if (!childBeingDeleted) {
					child->SetParent(nullptr);
				}
			}
			objects.erase(objects.begin() + idx);
		}
	}

	// Clean up InstancedGroups associated with any deleted scatter parents.
	// This MUST happen after removing objects but before pushing the undo action,
	// because the instanced data (GPU buffers) cannot be meaningfully restored by undo.
	for (auto& entry : undoEntries) {
		std::string name = entry.name;
		if (name.find("Scatter_Group_") == 0) {
			std::string idStr = name.substr(14); // everything after "Scatter_Group_"
			std::string prefix = "Scatter_Instanced_" + idStr;

			// Collect matching groups first, then remove (to avoid iterator invalidation)
			std::vector<std::string> toRemove;
			for (auto* group : instancedGroups) {
				if (group && group->GetName().find(prefix) == 0) {
					toRemove.push_back(group->GetName());
				}
			}
			for (const auto& groupName : toRemove) {
				RemoveInstancedGroup(groupName);
			}
		}
	}

	// Push undo action (it now owns the deleted objects' memory)
	undoManager.PushAction(std::make_unique<DeleteObjectsAction>(this, undoEntries, prevSelection));

	ClearSelection();
}

void SceneManager::DeleteSelectedLights()
{
	if (selectedLightIndices.empty()) return;

	// Snapshot for undo
	std::vector<DeletedLightEntry> undoEntries;
	std::vector<int> prevSelection = selectedLightIndices;

	// Sort descending to maintain indices
	std::vector<int> sorted = selectedLightIndices;
	std::sort(sorted.rbegin(), sorted.rend());

	for (int idx : sorted) {
		if (idx < 0 || idx >= (int)lights.size()) continue;
		LightObject* lo = lights[idx];

		DeletedLightEntry entry;
		entry.light = lo;
		entry.originalIndex = idx;
		entry.type = lo->GetLightType();
		entry.name = lo->GetName();
		entry.color = *lo->GetColorPtr();
		entry.ambientIntensity = *lo->GetAmbientIntensityPtr();
		entry.diffuseIntensity = *lo->GetDiffuseIntensityPtr();
		if (lo->GetPositionPtr()) entry.position = *lo->GetPositionPtr();
		if (lo->GetDirectionPtr()) entry.direction = *lo->GetDirectionPtr();
		if (lo->GetConstantPtr()) entry.constant = *lo->GetConstantPtr();
		if (lo->GetLinearPtr()) entry.linear = *lo->GetLinearPtr();
		if (lo->GetExponentPtr()) entry.exponent = *lo->GetExponentPtr();
		if (entry.type == LightType::Spot && lo->GetSpotEdgePtr())
			entry.edge = *lo->GetSpotEdgePtr();

		undoEntries.push_back(entry);
	}

	// Now actually delete
	for (int idx : sorted) {
		DeleteLight(idx);
	}

	// Push undo action
	undoManager.PushAction(std::make_unique<DeleteLightsAction>(this, undoEntries, prevSelection));

	ClearSelection();
}

void SceneManager::DeleteGameObject(int index)
{
	if (index < 0 || index >= (int)objects.size()) return;

	GameObject* obj = objects[index];

	std::string name = obj->GetName();
	if (name.find("Scatter_Group_") == 0) {
		std::string idStr = name.substr(14);
		std::string prefix = "Scatter_Instanced_" + idStr;
		
		std::vector<std::string> toRemove;
		for (auto* group : instancedGroups) {
			if (group && group->GetName().find(prefix) == 0) {
				toRemove.push_back(group->GetName());
			}
		}
		for (const auto& groupName : toRemove) {
			RemoveInstancedGroup(groupName);
		}
	}	// Recursive deletion: delete all children first
	// We make a copy of the children vector because deleting a child 
	// will modify the original vector via the destructor/parent detachment
	std::vector<GameObject*> childrenCopy = obj->GetChildren();
	for (auto* child : childrenCopy) {
		// Find child index in global list
		auto it = std::find(objects.begin(), objects.end(), child);
		if (it != objects.end()) {
			DeleteGameObject((int)(it - objects.begin()));
		}
	}

	// Now delete 'obj' itself
	// Destructor will handle parent detachment
	delete obj;
	objects.erase(objects.begin() + index);

	// Update selection indices
	std::vector<int> newSelection;
	for (int selIdx : selectedObjectIndices) {
		if (selIdx == index) continue;
		if (selIdx > index) newSelection.push_back(selIdx - 1);
		else newSelection.push_back(selIdx);
	}
	selectedObjectIndices = newSelection;

	if (selectedObjectIndices.empty()) activeDragAxis = 0;
}

GameObject* SceneManager::FindObject(const std::string& name)
{
	for (auto* obj : objects)
	{
		if (obj->GetName() == name) return obj;
	}
	return nullptr;
}

void SceneManager::RenderAll(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
	DirectionalLight* dLight, PointLight* pLights, unsigned int pCount,
	SpotLight* sLights, unsigned int sCount,
	float time, const Frustum* frustum, Shader* overrideShader, float screenWidth, float screenHeight, class Renderer* renderer, 
	GLuint sceneDepthTexture, GLuint reflectionTexture, glm::vec4 clipPlane, glm::mat4 shadowTransform, const GraphicsSettings* gs)
{
	struct Batch {
		Mesh* mesh;
		Material* material;
		Texture* texture;
		Texture* normalMap;
		bool isSelected;
		std::vector<glm::mat4> matrices;
	};
	std::vector<Batch> batchList;

	GLuint lastShaderID = 0;

	auto PrepareShader = [&](Shader* s) {
		if (!s) return;
		if (s->GetShaderID() != lastShaderID) {
			s->UseShader();
			lastShaderID = s->GetShaderID();

			// Upload Globals
			GLint projLoc = s->GetProjectionLocation();
			GLint viewLoc = s->GetViewLocation();
			GLint eyeLoc = s->GetEyePositionLocation();
			GLint timeLoc = glGetUniformLocation(s->GetShaderID(), "time");

			if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
			if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
			if (eyeLoc != -1) glUniform3f(eyeLoc, cameraPos.x, cameraPos.y, cameraPos.z);
			if (timeLoc != -1) glUniform1f(timeLoc, time);
			
			GLint clipLoc = glGetUniformLocation(s->GetShaderID(), "clipPlane");
			if (clipLoc != -1) glUniform4fv(clipLoc, 1, glm::value_ptr(clipPlane));
			
			GLint depthMapLoc = glGetUniformLocation(s->GetShaderID(), "sceneDepthMap");
			if (depthMapLoc != -1) {
				glActiveTexture(GL_TEXTURE14);
				glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
				glUniform1i(depthMapLoc, 14); // Binding 14
			}

			GLint reflectionMapLoc = glGetUniformLocation(s->GetShaderID(), "reflectionMap");
			if (reflectionMapLoc != -1) {
				glActiveTexture(GL_TEXTURE15);
				glBindTexture(GL_TEXTURE_2D, reflectionTexture);
				glUniform1i(reflectionMapLoc, 15); // Binding 15
			}

			// Screen size is required for depth sampling via gl_FragCoord
			GLint screenSizeLoc = glGetUniformLocation(s->GetShaderID(), "screenSize");
			if (screenSizeLoc != -1) glUniform2f(screenSizeLoc, screenWidth > 0.0f ? screenWidth : 1920.0f, screenHeight > 0.0f ? screenHeight : 1080.0f);

			if (dLight) s->SetDirectionalLight(dLight);
			if (pLights) s->SetPointLights(pLights, pCount, 4, 0);
			if (sLights) s->SetSpotLights(sLights, sCount, 4 + pCount, pCount);

			if (dLight) {
				// For CSM, the shadow map binding and matrix updates are handled by the Renderer
				// before calling RenderAll, to avoid redundant state changes in the batch loop.
				// However, if we are not using an override shader (main pass), we ensure split distances are set.
				if (!overrideShader) {
					const auto& matrices = dLight->GetCascadedLightMatrices();
					const auto& splits = dLight->GetCascadeSplitDistances();
					if (!matrices.empty()) {
						glUniformMatrix4fv(glGetUniformLocation(s->GetShaderID(), "directionalLightTransform"), (GLsizei)matrices.size(), GL_FALSE, glm::value_ptr(matrices[0]));
						glUniform1fv(glGetUniformLocation(s->GetShaderID(), "cascadeSplits"), (GLsizei)splits.size(), &splits[0]);
					}
					// View matrix for depth calculation
					glUniformMatrix4fv(glGetUniformLocation(s->GetShaderID(), "viewMatrix"), 1, GL_FALSE, glm::value_ptr(view));

					// Bind shadow maps for main pass shaders
					s->SetDirectionalShadowMap(3);
					s->SetDirectionalShadowColorMap(20);
				}
			}
		}
	};

	std::unordered_set<GameObject*> selectedObjs;
	if (!overrideShader) {
		for (int idx : selectedObjectIndices) {
			if (idx >= 0 && idx < (int)objects.size()) {
				selectedObjs.insert(objects[idx]);
			}
		}
	}

	auto RenderQueue = [&](const std::vector<GameObject*>& queue) {
		std::vector<Batch> localBatchList;

		// 1. Collect and Render Single Objects
		for (auto* obj : queue) {
		Mesh* msh = obj->GetMesh();
		Model* mdl = obj->GetModel();
		Material* mat = obj->GetMaterial();
		Texture* tex = obj->GetTexture();
		Texture* norm = obj->GetNormalMap();

		if (!msh && !mdl) continue;

		// ===== MULTI-LAYER CULLING PIPELINE =====
		if (frustum) {
			glm::vec3 bmin, bmax;
			obj->GetWorldBounds(bmin, bmax);

			// SAFETY CHECK: If the camera is horizontally inside the box, 
			// skip ALL culling. This prevents large floors/terrains from 
			// disappearing when the camera is at the edge but still on top.
			bool cameraInside = (cameraPos.x >= bmin.x && cameraPos.x <= bmax.x && 
								 cameraPos.z >= bmin.z && cameraPos.z <= bmax.z);

			if (!cameraInside) 
			{
				// LAYER 0: Global Distance Culling (Accurate AABB distance)
				float dx = glm::max(bmin.x - cameraPos.x, glm::max(0.0f, cameraPos.x - bmax.x));
				float dy = glm::max(bmin.y - cameraPos.y, glm::max(0.0f, cameraPos.y - bmax.y));
				float dz = glm::max(bmin.z - cameraPos.z, glm::max(0.0f, cameraPos.z - bmax.z));
				float distSq = dx*dx + dy*dy + dz*dz;

				float maxDist = 2000.0f;
				if (graphicsSettings) {
					maxDist = overrideShader ? graphicsSettings->shadowDistance : graphicsSettings->renderDistance;
				}
				if (distSq > maxDist * maxDist) continue;

				glm::vec3 sphereCenter; float sphereRadius;
				obj->GetWorldBoundingSphere(sphereCenter, sphereRadius);

				// LAYER 1: Bounding Sphere vs Frustum
				if (!frustum->IsSphereVisible(sphereCenter, sphereRadius)) continue;

				// LAYER 2: Contribution culling
				if (!overrideShader && screenHeight > 0.0f) {
					if (!Frustum::IsLargeEnough(sphereCenter, sphereRadius, 2.0f, projection, screenHeight, cameraPos)) continue;
				}

				// LAYER 3: AABB vs Frustum
				if (!frustum->IsBoxVisible(bmin, bmax)) continue;
			}
		}

		Shader* targetShader = overrideShader ? overrideShader : ((mat && mat->GetShader()) ? mat->GetShader() : mainShader);
		if (!targetShader) continue;

		bool hasLayers = !obj->GetTextureLayers().empty();
		bool isPickingPass = (overrideShader && pickingInitialized && overrideShader->GetShaderID() == pickingShader.GetShaderID());
		
		bool isSelected = false;
		// Only highlight if there are multiple objects selected
		if (!overrideShader && selectedObjs.size() > 1) {
			isSelected = selectedObjs.find(obj) != selectedObjs.end();
		}

		// If it's a simple instanced primitive without layers, batch it (unless it's picking pass which requires unique IDs)
			if (msh && !mdl && msh->IsInstanced() && !hasLayers && !isPickingPass) {
				bool found = false;
				for (auto& b : localBatchList) {
					if (b.mesh == msh && b.material == mat && b.texture == tex && b.normalMap == norm && b.isSelected == isSelected) {
						b.matrices.push_back(obj->GetWorldMatrix());
						found = true;
						break;
					}
				}
				if (!found) localBatchList.push_back({ msh, mat, tex, norm, isSelected, {obj->GetWorldMatrix()} });
			} else {
			// Render Single
			PrepareShader(targetShader);

			// === GPU TESSELLATION PATH ===
			// Fully automatic: TES uses per-layer displacement maps/scales directly,
			// TCS auto-computes tessellation level from maxLayerTiling.
			bool renderAsTessellated = false;
			if (obj->GetUseTessellation() && renderer) {
				bool hasDispMap = false;
				float maxTiling = 1.0f;
				for (const auto& layer : obj->GetTextureLayers()) {
					if (layer.displacementMap) {
						hasDispMap = true;
						if (layer.tiling > maxTiling) maxTiling = layer.tiling;
					}
				}

				if (hasDispMap && msh) {
					Shader* tessShaderToUse = nullptr;
					
					// Detect if we are in a shadow pass
					bool isShadowPass = (overrideShader && renderer && overrideShader->GetShaderID() == renderer->GetDirectionalShadowShader().GetShaderID());
					
					if (isShadowPass) {
						tessShaderToUse = &renderer->GetTessShadowShader();
					} else if (!overrideShader) {
						tessShaderToUse = &renderer->GetTessShader();
					}

					if (tessShaderToUse && tessShaderToUse->GetShaderID() != 0) {
						renderAsTessellated = true;
						PrepareShader(tessShaderToUse);
						targetShader = tessShaderToUse;
						GLuint sid = tessShaderToUse->GetShaderID();
						
						// Auto-set max tiling for TCS tessellation level calculation
						GLint maxTilingLoc = glGetUniformLocation(sid, "maxLayerTiling");
						if (maxTilingLoc != -1) glUniform1f(maxTilingLoc, maxTiling);

						// For shadow tessellation, we need to manually pass the light transform
						if (isShadowPass) {
							GLint lightTransLoc = glGetUniformLocation(sid, "directionalLightTransform");
							if (lightTransLoc != -1) {
								glUniformMatrix4fv(lightTransLoc, 1, GL_FALSE, glm::value_ptr(shadowTransform));
							}
						}
					}
				}
			}



			// Upload material alpha for shadow pass dithering
			obj->RenderSingle(
				targetShader->GetModelLocation(),
				targetShader->GetSpecularIntensityLocation(),
				targetShader->GetShininessLocation(),
				glGetUniformLocation(targetShader->GetShaderID(), "material.baseColor"),
				targetShader->GetTilingLocation(),
				targetShader->GetOffsetLocation(),
				glGetUniformLocation(targetShader->GetShaderID(), "useNormalMap"),
				glGetUniformLocation(targetShader->GetShaderID(), "useDiffuseTexture"),
				glGetUniformLocation(targetShader->GetShaderID(), "theTexture"),
				glGetUniformLocation(targetShader->GetShaderID(), "normalMap"),
				cameraPos,
				graphicsSettings,
				targetShader->GetShaderID()
			);
		}
	}

	// 2. Render Batches
	for (auto& b : localBatchList) {
		if (b.matrices.empty()) continue;

		Shader* targetShader = overrideShader ? overrideShader : ((b.material && b.material->GetShader()) ? b.material->GetShader() : mainShader);
		if (!targetShader) continue;

		PrepareShader(targetShader);



		if (!overrideShader && b.material) {
			b.material->UseMaterial(
				targetShader->GetSpecularIntensityLocation(),
				targetShader->GetShininessLocation(),
				glGetUniformLocation(targetShader->GetShaderID(), "material.baseColor"),
				targetShader->GetTilingLocation(),
				targetShader->GetOffsetLocation()
			);
			b.material->Bind();
		} else if (!overrideShader && !b.material) {
			glUniform1f(targetShader->GetSpecularIntensityLocation(), 0.0f);
			glUniform1f(targetShader->GetShininessLocation(), 1.0f);
			glUniform4f(glGetUniformLocation(targetShader->GetShaderID(), "material.baseColor"), 1.0f, 1.0f, 1.0f, 1.0f);
			glUniform2f(targetShader->GetTilingLocation(), 1.0f, 1.0f);
			glUniform2f(targetShader->GetOffsetLocation(), 0.0f, 0.0f);
		}

		// Apply texture overrides
		GLint useDiffuseLoc = glGetUniformLocation(targetShader->GetShaderID(), "useDiffuseTexture");
		GLint texLoc = glGetUniformLocation(targetShader->GetShaderID(), "theTexture");
		if (!overrideShader && b.texture) {
			if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, 1);
			if (texLoc != -1) glUniform1i(texLoc, 0);
			b.texture->UseTexture();
		} else {
			if (useDiffuseLoc != -1) glUniform1i(useDiffuseLoc, 0);
		}

		// Apply normal map overrides
		GLint useNormalLoc = glGetUniformLocation(targetShader->GetShaderID(), "useNormalMap");
		GLint normLoc = glGetUniformLocation(targetShader->GetShaderID(), "normalMap");
		if (!overrideShader && b.normalMap) {
			if (useNormalLoc != -1) glUniform1i(useNormalLoc, 1);
			if (normLoc != -1) glUniform1i(normLoc, 1);
			b.normalMap->UseNormalMap();
		} else {
			if (useNormalLoc != -1) glUniform1i(useNormalLoc, 0);
		}

		// Explicitly disable texture layers for batched objects to prevent leakage from single-rendered objects
		GLint uLayerCountLoc = glGetUniformLocation(targetShader->GetShaderID(), "textureLayerCount");
		if (uLayerCountLoc != -1) glUniform1i(uLayerCountLoc, 0);

		GLint instLoc = glGetUniformLocation(targetShader->GetShaderID(), "useInstancing");
		if (instLoc != -1) glUniform1i(instLoc, 1);
		
		b.mesh->RenderInstancedMesh((unsigned int)b.matrices.size(), b.matrices.data());
		
		if (instLoc != -1) glUniform1i(instLoc, 0);
	}
	}; // end RenderQueue

	std::vector<GameObject*> opaqueObjects;
	std::vector<GameObject*> transparentObjects;
	std::unordered_set<GameObject*> processed;

	std::function<void(GameObject*)> CollectRecursive = [&](GameObject* obj) {
		if (!obj || processed.find(obj) != processed.end()) return;
		processed.insert(obj);

		if (obj->GetMesh() || obj->GetModel()) {
			Material* mat = obj->GetMaterial();
			// If it's an override shader pass (like shadows), we don't care about sorting or depth masks; render all in opaque bucket
			if (overrideShader || (mat && mat->GetAlpha() >= 0.99f) || !mat) {
				opaqueObjects.push_back(obj);
			} else {
				transparentObjects.push_back(obj);
			}
		}

		for (auto* child : obj->GetChildren()) {
			CollectRecursive(child);
		}
	};

	for (auto* obj : objects) {
		// Only start recursion from roots or objects whose parents are NOT in the scene list
		// This ensures we catch everything exactly once regardless of how it was added.
		bool parentInScene = false;
		if (obj->GetParent()) {
			// Check if parent is also in the objects list
			for (auto* p : objects) if (p == obj->GetParent()) { parentInScene = true; break; }
		}

		if (!parentInScene) {
			CollectRecursive(obj);
		}
	}

	// 1. Render all opaque objects normally (writes depth)
	RenderQueue(opaqueObjects);

	// 2. Sort and Render transparent objects
	if (!transparentObjects.empty() && !overrideShader) {
		// Painter's algorithm: Back-to-front sorting relative to camera location
		std::sort(transparentObjects.begin(), transparentObjects.end(), [&](GameObject* a, GameObject* b) {
			float distA = glm::length(glm::vec3(a->GetWorldMatrix()[3]) - cameraPos);
			float distB = glm::length(glm::vec3(b->GetWorldMatrix()[3]) - cameraPos);
			return distA > distB;
		});

		// Render with Z-Write OFF so overlapping transparent objects don't clip each other
		glDepthMask(GL_FALSE);

		if (sceneDepthTexture > 0) {
			glActiveTexture(GL_TEXTURE14);
			glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
		}

		RenderQueue(transparentObjects);
		glDepthMask(GL_TRUE); // Restore depth mask
	}

	// ================================================================
	// GPU-Driven Instanced Groups (grass, foliage, rocks, etc.)
	// These bypass the entire GameObject pipeline for maximum performance.
	// NOTE: Shadow pass for instanced groups is handled separately by
	// Renderer::DirectionalShadowMapPass() via CullAndDrawShadow(),
	// which uses a dedicated instanced shadow vertex shader.
	// We skip instanced groups here when overrideShader is set (shadow/picking pass).
	// ================================================================
	if (!overrideShader && !instancedGroups.empty() && cullShader && instancedRenderShader) {
		// Sanitize GL state before GPU-driven instanced rendering.
		// Previous passes (transparent objects, texture layers, SSAO quad) may leave
		// stale state that causes intermittent rendering glitches (hollow/dark meshes)
		// especially on AMD drivers.
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Unbind any stale VAO/VBO state from previous draw calls
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		// Disable face culling for instanced foliage — grass blades, flowers, etc.
		// are thin double-sided geometry that must be visible from both sides.
		glDisable(GL_CULL_FACE);

		Shader* lastShader = nullptr;

		for (auto* group : instancedGroups) {
			if (!group) continue;

			// Resolve the correct shader for this group's material
			// We try to inherit the original fragment shader while using our instanced vertex logic
			Shader* targetRenderShader = instancedRenderShader;
			if (renderer && group->GetMaterial() && group->GetMaterial()->GetShader()) {
				targetRenderShader = renderer->GetInstancedShader(group->GetMaterial()->GetShader());
			}

			// Only bind lighting if the shader has changed (optimization)
			if (targetRenderShader != lastShader) {
				targetRenderShader->UseShader();
				GLuint sid = targetRenderShader->GetShaderID();

				if (dLight) {
					targetRenderShader->SetDirectionalLight(dLight);
					
					const auto& matrices = dLight->GetCascadedLightMatrices();
					const auto& splits = dLight->GetCascadeSplitDistances();
					if (!matrices.empty()) {
						glUniformMatrix4fv(glGetUniformLocation(sid, "dirLightMatrices"), (GLsizei)matrices.size(), GL_FALSE, glm::value_ptr(matrices[0]));
						glUniform1fv(glGetUniformLocation(sid, "cascadeSplits"), (GLsizei)splits.size(), &splits[0]);
					}
					glUniformMatrix4fv(glGetUniformLocation(sid, "viewMatrix"), 1, GL_FALSE, glm::value_ptr(view));
					
					dLight->GetShadowMap()->Read(GL_TEXTURE3);
					targetRenderShader->SetDirectionalShadowMap(3);
				}
				if (pLights) targetRenderShader->SetPointLights(pLights, pCount, 4, 0);
				if (sLights) targetRenderShader->SetSpotLights(sLights, sCount, 4 + pCount, pCount);

				GLint timeLoc = glGetUniformLocation(sid, "time");
				if (timeLoc != -1) glUniform1f(timeLoc, time);

				GLint clipLoc = glGetUniformLocation(sid, "clipPlane");
				if (clipLoc != -1) glUniform4fv(clipLoc, 1, glm::value_ptr(clipPlane));

				GLint depthMapLoc = glGetUniformLocation(sid, "sceneDepthMap");
				if (depthMapLoc != -1) {
					glActiveTexture(GL_TEXTURE14);
					glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
					glUniform1i(depthMapLoc, 14);
				}

				GLint reflectionMapLoc = glGetUniformLocation(sid, "reflectionMap");
				if (reflectionMapLoc != -1) {
					glActiveTexture(GL_TEXTURE15);
					glBindTexture(GL_TEXTURE_2D, reflectionTexture);
					glUniform1i(reflectionMapLoc, 15);
				}

				GLint screenSizeLoc = glGetUniformLocation(sid, "screenSize");
				if (screenSizeLoc != -1) glUniform2f(screenSizeLoc, screenHeight > 0.0f ? (screenHeight * ((projection[0][0]) > 0 ? (projection[1][1] / projection[0][0]) : 1.77f)) : 1920.0f, screenHeight > 0.0f ? screenHeight : 1080.0f);

				// Pass shadow distance to instanced shader for percentage-based fade
				if (dLight) {
					GLint sdLoc = glGetUniformLocation(sid, "shadowDistance");
					if (sdLoc != -1) {
						const auto& splits = dLight->GetCascadeSplitDistances();
						float shadowFar = splits.empty() ? dLight->GetShadowFrustumSize() : splits.back();
						glUniform1f(sdLoc, shadowFar);
					}
					// Also pass shadow color map for instanced objects
					dLight->GetShadowMap()->ReadColor(GL_TEXTURE20);
					GLint scmLoc = glGetUniformLocation(sid, "directionalShadowColorMap");
					if (scmLoc != -1) glUniform1i(scmLoc, 20);
				}
				
				lastShader = targetRenderShader;
			}

			group->CullAndDraw(
				cullShader->GetShaderID(),
				*targetRenderShader,
				projection, view, cameraPos,
				gs ? gs : graphicsSettings,
				false
			);
		}

		glEnable(GL_CULL_FACE);
	}

	// ================================================================
	// Selection Wireframe Overlay (multi-select only)
	// Dead-simple approach: render selected objects as bright wireframe
	// using the gizmo shader. Works with ANY mesh/model regardless of
	// material or custom shader — no selectionTint uniform needed.
	// ================================================================
	if (!overrideShader && selectedObjs.size() > 1 && gizmoShader.GetShaderID()) {
		// ... (existing wireframe code)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(2.0f);
		glDepthFunc(GL_LEQUAL);
		glDisable(GL_CULL_FACE);
		gizmoShader.UseShader();
		glUniformMatrix4fv(gizmoShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(gizmoShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
		GLint colorLoc = glGetUniformLocation(gizmoShader.GetShaderID(), "gizmoColor");
		GLint modelLoc = gizmoShader.GetModelLocation();
		glUniform3f(colorLoc, 1.0f, 0.5f, 0.0f);
		for (auto* obj : selectedObjs) {
			glm::mat4 modelMatrix = obj->GetWorldMatrix();
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelMatrix));
			if (obj->GetModel()) obj->GetModel()->RenderModel(-1, -1, -1, -1);
			else if (obj->GetMesh()) obj->GetMesh()->RenderMesh();
		}
	}

	// Debug Bounds Visualization
	if (!overrideShader && graphicsSettings && graphicsSettings->debugShowBounds && gizmoShader.GetShaderID()) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDisable(GL_CULL_FACE);
		gizmoShader.UseShader();
		glUniformMatrix4fv(gizmoShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(gizmoShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));
		GLint colorLoc = glGetUniformLocation(gizmoShader.GetShaderID(), "gizmoColor");
		GLint modelLoc = gizmoShader.GetModelLocation();
		glUniform3f(colorLoc, 0.0f, 1.0f, 1.0f); // Cyan for bounds

		for (auto* obj : objects) {
			glm::vec3 center; float radius;
			obj->GetWorldBoundingSphere(center, radius);
			
			// Simple debug box if we don't have a sphere mesh handy
			glm::mat4 boxMatrix = glm::translate(glm::mat4(1.0f), center);
			boxMatrix = glm::scale(boxMatrix, glm::vec3(radius * 2.0f));
			glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(boxMatrix));
			
			// Use iconMesh as a proxy for bounds (will show as a quad, but better than nothing)
			if (iconMesh) iconMesh->RenderMesh();
		}
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glEnable(GL_CULL_FACE);
	}
}


void SceneManager::AddLight(LightObject* light)
{
	if (light) lights.push_back(light);
}

void SceneManager::Clear()
{
	for (auto* obj : objects) delete obj;
	objects.clear();
	
	for (auto* light : lights) delete light;
	lights.clear();

	ClearInstancedGroups();
	
	selectedObjectIndices.clear();
	selectedLightIndices.clear();
	nodeGraph.Clear();
	undoManager.Clear();
}

// =====================================================================
// Undo Helpers (no memory management, no undo recording)
// =====================================================================

void SceneManager::InsertObjectAt(GameObject* obj, int index)
{
	if (!obj) return;
	if (index < 0 || index > (int)objects.size()) index = (int)objects.size();
	objects.insert(objects.begin() + index, obj);
}

void SceneManager::RemoveObjectRaw(int index)
{
	if (index < 0 || index >= (int)objects.size()) return;
	// DO NOT delete the object — caller (undo action) owns the memory
	objects.erase(objects.begin() + index);

	// Update selection indices
	std::vector<int> newSelection;
	for (int selIdx : selectedObjectIndices) {
		if (selIdx == index) continue;
		if (selIdx > index) newSelection.push_back(selIdx - 1);
		else newSelection.push_back(selIdx);
	}
	selectedObjectIndices = newSelection;
	if (selectedObjectIndices.empty()) activeDragAxis = 0;
}

// =====================================================================
// GPU-Driven Instanced Groups Management
// =====================================================================

void SceneManager::AddInstancedGroup(InstancedGroup* group)
{
	if (group) {
		// Remove any existing group with the same name
		RemoveInstancedGroup(group->GetName());
		instancedGroups.push_back(group);
		printf("[SceneManager] Added instanced group '%s' (%u instances)\n",
			group->GetName().c_str(), group->GetTotalCount());
	}
}

void SceneManager::RemoveInstancedGroup(const std::string& name)
{
	for (auto it = instancedGroups.begin(); it != instancedGroups.end(); ++it) {
		if ((*it)->GetName() == name) {
			delete *it;
			instancedGroups.erase(it);
			return;
		}
	}
}

void SceneManager::ClearInstancedGroups()
{
	for (auto* group : instancedGroups) delete group;
	instancedGroups.clear();
}

// =====================================================================
// DRY Helpers
// =====================================================================

glm::mat4 SceneManager::GetSelectedRotationMatrix() const
{
	glm::mat4 rot(1.0f);
	int sel = GetSelectedIndex();
	if (sel != -1) {
		glm::vec3 r = objects[sel]->GetTransform().GetRotation();
		rot = glm::rotate(rot, glm::radians(r.y), glm::vec3(0.0f, 1.0f, 0.0f));
		rot = glm::rotate(rot, glm::radians(r.x), glm::vec3(1.0f, 0.0f, 0.0f));
		rot = glm::rotate(rot, glm::radians(r.z), glm::vec3(0.0f, 0.0f, 1.0f));
	}
	return rot;
}

bool SceneManager::GetGizmoPosition(glm::vec3& outPos) const
{
	int selObj = GetSelectedIndex();
	if (selObj != -1) {
		glm::vec3 bMin, bMax;
		objects[selObj]->GetWorldBounds(bMin, bMax);
		outPos = (bMin + bMax) * 0.5f; // Center of the volume
		return true;
	}
	
	int selLight = GetSelectedLightIndex();
	if (selLight != -1) {
		glm::vec3* lightPos = lights[selLight]->GetPositionPtr();
		if (lightPos) { outPos = *lightPos; return true; }
	}
	return false;
}

std::string SceneManager::GetSelectedName() const
{
	int obj = GetSelectedIndex();
	int light = GetSelectedLightIndex();
	
	if (obj != -1 && obj < (int)objects.size())
		return objects[obj]->GetName();
	else if (light != -1 && light < (int)lights.size())
		return lights[light]->GetName();
	return "None";
}

// ========== Selection Implementation ==========

void SceneManager::SetSelectedIndex(int index, bool multiSelect, bool rangeSelect)
{
	if (!multiSelect && !rangeSelect) {
		selectedObjectIndices.clear();
		selectedLightIndices.clear();
	}

	if (index < 0) return;
	selectedLightIndices.clear(); // Cannot select both lights and objects in multi-select for now

	if (rangeSelect && !selectedObjectIndices.empty()) {
		int start = selectedObjectIndices.back();
		int end = index;
		if (start > end) std::swap(start, end);
		
		for (int i = start; i <= end; i++) {
			if (!IsObjectSelected(i)) selectedObjectIndices.push_back(i);
		}
	} else if (multiSelect) {
		auto it = std::find(selectedObjectIndices.begin(), selectedObjectIndices.end(), index);
		if (it != selectedObjectIndices.end()) {
			selectedObjectIndices.erase(it);
		} else {
			selectedObjectIndices.push_back(index);
		}
	} else {
		selectedObjectIndices.push_back(index);
	}
	activeDragAxis = 0;
}

int SceneManager::GetSelectedIndex() const 
{ 
	return selectedObjectIndices.empty() ? -1 : selectedObjectIndices.back(); 
}

bool SceneManager::IsObjectSelected(int index) const 
{
	return std::find(selectedObjectIndices.begin(), selectedObjectIndices.end(), index) != selectedObjectIndices.end();
}

void SceneManager::SetSelectedLightIndex(int index, bool multiSelect, bool rangeSelect)
{
	if (!multiSelect && !rangeSelect) {
		selectedObjectIndices.clear();
		selectedLightIndices.clear();
	}

	if (index < 0) return;
	selectedObjectIndices.clear();

	if (rangeSelect && !selectedLightIndices.empty()) {
		int start = selectedLightIndices.back();
		int end = index;
		if (start > end) std::swap(start, end);
		for (int i = start; i <= end; i++) {
			if (!IsLightSelected(i)) selectedLightIndices.push_back(i);
		}
	} else if (multiSelect) {
		auto it = std::find(selectedLightIndices.begin(), selectedLightIndices.end(), index);
		if (it != selectedLightIndices.end()) {
			selectedLightIndices.erase(it);
		} else {
			selectedLightIndices.push_back(index);
		}
	} else {
		selectedLightIndices.push_back(index);
	}
	activeDragAxis = 0;
}

int SceneManager::GetSelectedLightIndex() const 
{ 
	return selectedLightIndices.empty() ? -1 : selectedLightIndices.back(); 
}

bool SceneManager::IsLightSelected(int index) const 
{
	return std::find(selectedLightIndices.begin(), selectedLightIndices.end(), index) != selectedLightIndices.end();
}

void SceneManager::BoxSelect(glm::vec2 rectMin, glm::vec2 rectMax, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight, bool additive, GLuint depthFBO)
{
	if (!additive) {
		selectedObjectIndices.clear();
		selectedLightIndices.clear();
		for (auto* group : instancedGroups) if (group) group->ClearSelection();
	}

	glm::mat4 vp = projection * view;

	// Helper: project world position to viewport pixel coordinates
	// Returns false if behind camera
	auto ProjectToScreen = [&](glm::vec3 worldPos, glm::vec2& screenPos, float& outDepth) -> bool {
		glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
		if (clip.w <= 0.0f) return false; // Behind camera

		glm::vec3 ndc = glm::vec3(clip) / clip.w;
		// NDC to viewport pixel coords
		screenPos.x = (ndc.x * 0.5f + 0.5f) * viewportWidth;
		screenPos.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight; // Y flipped (0 at top)
		outDepth = ndc.z * 0.5f + 0.5f; // Map [-1, 1] to [0, 1]
		return true;
	};

	// Optional: Read depth buffer for occlusion culling
	std::vector<float> depthBuffer;
	int rX = (int)rectMin.x;
	int rY = (int)(viewportHeight - rectMax.y); // OpenGL Y starts at bottom
	int rW = (int)(rectMax.x - rectMin.x);
	int rH = (int)(rectMax.y - rectMin.y);

	if (depthFBO != 0) {
		// Clamp to viewport bounds
		if (rX < 0) { rW += rX; rX = 0; }
		if (rY < 0) { rH += rY; rY = 0; }
		if (rX + rW > (int)viewportWidth) rW = (int)viewportWidth - rX;
		if (rY + rH > (int)viewportHeight) rH = (int)viewportHeight - rY;

		if (rW > 0 && rH > 0) {
			depthBuffer.resize(rW * rH);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, depthFBO);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(rX, rY, rW, rH, GL_DEPTH_COMPONENT, GL_FLOAT, depthBuffer.data());
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}
	}

	auto IsVisible = [&](glm::vec2 screenPos, float depth) -> bool {
		if (depthBuffer.empty()) return true;

		int lx = (int)screenPos.x - rX;
		int ly = (int)(viewportHeight - screenPos.y) - rY;
		if (ly < 0) ly = 0;
		if (ly >= rH) ly = rH - 1;

		if (lx >= 0 && lx < rW) {
			float sampledDepth = depthBuffer[lx + ly * rW];
			// Bias to account for floating point precision and self-occlusion.
			// Increased to 0.001f for better tolerance on slopes.
			return depth <= sampledDepth + 0.001f; 
		}
		return false;
	};

	// Select regular objects
	for (int i = 0; i < (int)objects.size(); i++) {
		glm::vec3 worldPos = glm::vec3(objects[i]->GetWorldMatrix()[3]);
		glm::vec2 screenPos;
		float depth;
		if (!ProjectToScreen(worldPos, screenPos, depth)) continue;

		if (screenPos.x >= rectMin.x && screenPos.x <= rectMax.x &&
			screenPos.y >= rectMin.y && screenPos.y <= rectMax.y) {
			
			if (IsVisible(screenPos, depth)) {
				if (!IsObjectSelected(i)) {
					selectedObjectIndices.push_back(i);
				}
			}
		}
	}

	// Select lights  
	if (selectedObjectIndices.empty()) {
		for (int i = 0; i < (int)lights.size(); i++) {
			glm::vec3* lightPos = lights[i]->GetPositionPtr();
			if (!lightPos) continue;

			glm::vec2 screenPos;
			float depth;
			if (!ProjectToScreen(*lightPos, screenPos, depth)) continue;

			if (screenPos.x >= rectMin.x && screenPos.x <= rectMax.x &&
				screenPos.y >= rectMin.y && screenPos.y <= rectMax.y) {
				
				if (IsVisible(screenPos, depth)) {
					if (!IsLightSelected(i)) {
						selectedLightIndices.push_back(i);
					}
				}
			}
		}
	}

	// Extract instanced scatter objects that fall within the box
	for (auto* group : instancedGroups) {
		if (!group || group->cpuInstances.empty()) continue;

		std::vector<int> matchingIndices;
		for (int i = 0; i < (int)group->cpuInstances.size(); i++) {
			const auto& inst = group->cpuInstances[i];
			glm::vec3 worldPos(inst.positionAndScale.x, inst.positionAndScale.y, inst.positionAndScale.z);

			glm::vec2 screenPos;
			float depth;
			if (!ProjectToScreen(worldPos, screenPos, depth)) continue;

			if (screenPos.x >= rectMin.x && screenPos.x <= rectMax.x &&
				screenPos.y >= rectMin.y && screenPos.y <= rectMax.y) {
				
				if (IsVisible(screenPos, depth)) {
					matchingIndices.push_back(i);
				}
			}
		}

		// Extract all instances in parallel
		if (!matchingIndices.empty()) {
			int baseIdx = (int)objects.size();
			group->ExtractInstances(matchingIndices, this, true);
			
			// Select the newly extracted objects
			int newSize = (int)objects.size();
			for (int i = baseIdx; i < newSize; i++) {
				if (!IsObjectSelected(i)) {
					selectedObjectIndices.push_back(i);
				}
			}

			// Single GPU re-upload after all extractions from this group
			group->ReuploadGPU();
		}
	}

	// If we selected both objects and lights, keep only objects
	if (!selectedObjectIndices.empty() && !selectedLightIndices.empty()) {
		selectedLightIndices.clear();
	}

	activeDragAxis = 0;
	
	printf("[SceneManager] Box selected %d objects, %d lights\n", 
		(int)selectedObjectIndices.size(), (int)selectedLightIndices.size());
}

// =====================================================================
// Creation / Deletion
// =====================================================================

void SceneManager::CreateGameObject(const std::string& type, glm::vec3 spawnPos)
{
	GameObject* newObj = new GameObject(type + " " + std::to_string(objects.size()));
	
	if (type == "Plane") newObj->SetMesh(PrimitiveGenerator::CreatePlane());
	else if (type == "Cube") newObj->SetMesh(PrimitiveGenerator::CreateCube());
	else if (type == "Sphere") newObj->SetMesh(PrimitiveGenerator::CreateSphere());

	newObj->SetPrimitiveType(type == "Empty Object" ? "Empty" : type);
	newObj->GetTransform().SetPosition(spawnPos);

	objects.push_back(newObj);
	SetSelectedIndex((int)objects.size() - 1);

	// Record undo action
	undoManager.PushAction(std::make_unique<CreateObjectAction>(this, std::vector<GameObject*>{newObj}, "Create " + type));
}

#include "AssetManager.h"

void SceneManager::InstantiateModel(const std::filesystem::path& path, glm::vec3 spawnPos)
{
	std::string baseName = path.stem().string();
	Model* model = AssetManager::Get().GetModel(path.string());
	if (!model) return;

	// If the model has multiple meshes, explode it into a modular hierarchy
	// (e.g. Tree -> [Trunk, Leaves])
	if (model->GetMeshCount() > 1) 
	{
		GameObject* root = new GameObject(baseName + " (Root)");
		root->GetTransform().SetPosition(spawnPos);
		root->SetModelSourcePath(path.string());
		objects.push_back(root);

		const auto& meshNames = model->GetMeshNames();
		for (size_t i = 0; i < model->GetMeshCount(); i++) 
		{
			std::string mName = meshNames[i];
			if (mName.empty() || mName == "default") mName = "Mesh_" + std::to_string(i);

			GameObject* child = new GameObject(mName);
			child->SetMesh(model->GetMesh(i));
			
			unsigned int matIdx = model->GetMaterialIndex((unsigned int)i);
			child->SetTexture(model->GetTexture(matIdx));
			child->SetNormalMap(model->GetNormalMap(matIdx));
			
			// Logic: Set the material to a default if not present
			if (!child->GetMaterial()) {
				child->SetMaterial(new Material());
			}

			// Logical Auto-Configuration: Detect Foliage/Leaves
			std::string lowerName = mName;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
			if (lowerName.find("leaf") != std::string::npos || 
				lowerName.find("leaves") != std::string::npos || 
				lowerName.find("foliage") != std::string::npos ||
				lowerName.find("polysurface1sg1") != std::string::npos) // Match based on Tree.obj structure
			{
				// Auto-assign wind parameters (matches grass.vert/frag uniforms)
				child->GetMaterial()->SetFloat("windSpeed", 1.0f);
				child->GetMaterial()->SetFloat("windStrength", 0.15f);
				child->SetName(mName + " (Foliage)");
			}
			else if (lowerName.find("bark") != std::string::npos || lowerName.find("trunk") != std::string::npos)
			{
				child->SetName(mName + " (Trunk)");
			}

			root->AddChild(child); // Attach to root without maintaining world position
			objects.push_back(child);
		}
		SetSelectedIndex((int)objects.size() - (int)model->GetMeshCount() - 1);
		printf("[SceneManager] Partitioned modular model '%s' into %d components.\n", baseName.c_str(), (int)model->GetMeshCount());

		// Record undo for all created objects (root + children)
		std::vector<GameObject*> created;
		created.push_back(root);
		for (size_t i = 0; i < model->GetMeshCount(); i++) {
			created.push_back(objects[objects.size() - model->GetMeshCount() + i]);
		}
		undoManager.PushAction(std::make_unique<CreateObjectAction>(this, created, "Instantiate " + baseName));
	}
	else 
	{
		// Fallback for single-mesh models
		GameObject* newObj = new GameObject(baseName + " " + std::to_string(objects.size()));
		newObj->GetTransform().SetPosition(spawnPos);
		newObj->SetModel(model);
		newObj->SetModelSourcePath(path.string());
		objects.push_back(newObj);
		SetSelectedIndex((int)objects.size() - 1);
		printf("[SceneManager] Instantiated single-mesh model: %s\n", baseName.c_str());

		// Record undo
		undoManager.PushAction(std::make_unique<CreateObjectAction>(this, std::vector<GameObject*>{newObj}, "Instantiate " + baseName));
	}
}

void SceneManager::CreateLight(LightType type, glm::vec3 spawnPos)
{
	if (type == LightType::Point) {
		if (globalPointLights && globalPointLightCount && *globalPointLightCount < MAX_POINT_LIGHTS) {
			unsigned int idx = *globalPointLightCount;
			globalPointLights[idx] = PointLight(1024, 1024, 0.01f, 100.0f, 1.0f, 1.0f, 1.0f, 0.1f, 0.8f, 0.0f, 5.0f, 0.0f, 1.0f, 0.02f, 0.01f);
			*globalPointLights[idx].GetPositionPtr() = spawnPos;
			
			LightObject* newLightObj = new LightObject("Point Light " + std::to_string(lights.size()), &globalPointLights[idx]);
			lights.push_back(newLightObj);
			(*globalPointLightCount)++;
			SetSelectedLightIndex((int)lights.size() - 1);

			// Record undo
			auto action = std::make_unique<CreateLightAction>(this, type, "Create Point Light");
			action->SetCreatedIndex((int)lights.size() - 1);
			undoManager.PushAction(std::move(action));
		}
	} else if (type == LightType::Spot) {
		if (globalSpotLights && globalSpotLightCount && *globalSpotLightCount < MAX_SPOT_LIGHTS) {
			unsigned int idx = *globalSpotLightCount;
			globalSpotLights[idx] = SpotLight(1024, 1024, 0.01f, 100.0f, 1.0f, 1.0f, 1.0f, 0.1f, 1.0f, 0.0f, 5.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.02f, 0.01f, 20.0f);
			*globalSpotLights[idx].GetPositionPtr() = spawnPos;
			
			LightObject* newLightObj = new LightObject("Spot Light " + std::to_string(lights.size()), &globalSpotLights[idx]);
			lights.push_back(newLightObj);
			(*globalSpotLightCount)++;
			SetSelectedLightIndex((int)lights.size() - 1);

			// Record undo
			auto action = std::make_unique<CreateLightAction>(this, type, "Create Spot Light");
			action->SetCreatedIndex((int)lights.size() - 1);
			undoManager.PushAction(std::move(action));
		}
	}
}

void SceneManager::DeleteLight(int index)
{
	if (index < 0 || index >= (int)lights.size()) return;

	LightObject* light = lights[index];
	LightType type = light->GetLightType();

	if (type == LightType::Directional) return;

	if (type == LightType::Point && globalPointLights && globalPointLightCount) {
		PointLight* ptr = light->GetPointLight();
		int arrayIdx = (int)(ptr - globalPointLights);
		if (arrayIdx >= 0 && arrayIdx < (int)*globalPointLightCount) {
			for (unsigned int j = arrayIdx; j < *globalPointLightCount - 1; j++)
				globalPointLights[j] = globalPointLights[j + 1];
			(*globalPointLightCount)--;

			for (auto* lo : lights) {
				if (lo == light) continue;
				if (lo->GetLightType() == LightType::Point && lo->GetPointLight() > ptr)
					lo->SetPointLight(lo->GetPointLight() - 1);
			}
		}
	}
	else if (type == LightType::Spot && globalSpotLights && globalSpotLightCount) {
		SpotLight* ptr = light->GetSpotLight();
		int arrayIdx = (int)(ptr - globalSpotLights);
		if (arrayIdx >= 0 && arrayIdx < (int)*globalSpotLightCount) {
			for (unsigned int j = arrayIdx; j < *globalSpotLightCount - 1; j++)
				globalSpotLights[j] = globalSpotLights[j + 1];
			(*globalSpotLightCount)--;

			for (auto* lo : lights) {
				if (lo == light) continue;
				if (lo->GetLightType() == LightType::Spot && lo->GetSpotLight() > ptr)
					lo->SetSpotLight(lo->GetSpotLight() - 1);
			}
		}
	}

	delete light;
	lights.erase(lights.begin() + index);
	selectedLightIndices.clear();
}

void SceneManager::CopySelected()
{
	ClearClipboard();

	// Copy Objects
	for (int idx : selectedObjectIndices) {
		if (idx >= 0 && idx < (int)objects.size()) {
			GameObject* original = objects[idx];
			// Clone recursively
			GameObject* clone = original->Clone(original->GetName() + " (Copy)");
			clipboardObjects.push_back(clone);
		}
	}

	// Copy Lights
	for (int idx : selectedLightIndices) {
		if (idx >= 0 && idx < (int)lights.size()) {
			LightObject* lo = lights[idx];
			LightClipboardEntry entry;
			entry.type = lo->GetLightType();
			entry.name = lo->GetName() + " (Copy)";
			entry.color = *lo->GetColorPtr();
			entry.ambientIntensity = *lo->GetAmbientIntensityPtr();
			entry.diffuseIntensity = *lo->GetDiffuseIntensityPtr();
			
			if (lo->GetPositionPtr()) entry.position = *lo->GetPositionPtr();
			if (lo->GetDirectionPtr()) entry.direction = *lo->GetDirectionPtr();
			if (lo->GetConstantPtr()) entry.constant = *lo->GetConstantPtr();
			if (lo->GetLinearPtr()) entry.linear = *lo->GetLinearPtr();
			if (lo->GetExponentPtr()) entry.exponent = *lo->GetExponentPtr();
			if (entry.type == LightType::Spot) entry.edge = *lo->GetSpotEdgePtr();
			
			clipboardLights.push_back(entry);
		}
	}

	if (!clipboardObjects.empty() || !clipboardLights.empty()) {
		printf("[SceneManager] Copied %d objects and %d lights to clipboard.\n", 
			(int)clipboardObjects.size(), (int)clipboardLights.size());
	}
}

void SceneManager::Paste()
{
	if (clipboardObjects.empty() && clipboardLights.empty()) return;

	ClearSelection();

	// Paste Objects
	for (GameObject* proto : clipboardObjects) {
		// Clone again so we can paste multiple times
		GameObject* clone = proto->Clone(proto->GetName());
		objects.push_back(clone);
		selectedObjectIndices.push_back((int)objects.size() - 1);
	}

	// Paste Lights
	for (const auto& entry : clipboardLights) {
		CreateLight(entry.type, entry.position);
		// The new light is now at lights.back(), and it's already selected by CreateLight
		LightObject* lo = lights.back();
		lo->SetName(entry.name);
		*lo->GetColorPtr() = entry.color;
		*lo->GetAmbientIntensityPtr() = entry.ambientIntensity;
		*lo->GetDiffuseIntensityPtr() = entry.diffuseIntensity;
		if (lo->GetDirectionPtr()) *lo->GetDirectionPtr() = entry.direction;
		if (lo->GetConstantPtr()) *lo->GetConstantPtr() = entry.constant;
		if (lo->GetLinearPtr()) *lo->GetLinearPtr() = entry.linear;
		if (lo->GetExponentPtr()) *lo->GetExponentPtr() = entry.exponent;
		if (entry.type == LightType::Spot) *lo->GetSpotEdgePtr() = entry.edge;
		
		// If we are pasting multiple lights, we need to add them to selection
		// CreateLight already adds one, but Paste might handle multiple
		if (clipboardLights.size() > 1) {
			if (!IsLightSelected((int)lights.size() - 1)) {
				selectedLightIndices.push_back((int)lights.size() - 1);
			}
		}
	}

	printf("[SceneManager] Pasted %d objects and %d lights.\n", 
		(int)clipboardObjects.size(), (int)clipboardLights.size());
}

void SceneManager::ClearClipboard()
{
	for (auto* obj : clipboardObjects) {
		// Since these are detached clones, we need a way to delete them.
		// However, GameObject destructor removes from parent.
		// But these aren't in the scene list, so it's safe.
		delete obj; 
	}
	clipboardObjects.clear();
	clipboardLights.clear();
}

// =====================================================================
// Picking System
// =====================================================================

static glm::vec3 EncodeID(int id)
{
	return glm::vec3(
		(((id & 0x0000FF) >> 0) + 0.5f) / 255.0f,
		(((id & 0x00FF00) >> 8) + 0.5f) / 255.0f,
		(((id & 0xFF0000) >> 16) + 0.5f) / 255.0f
	);
}

static int DecodeID(unsigned char pixel[3])
{
	return pixel[0] + pixel[1] * 256 + pixel[2] * 256 * 256;
}

void SceneManager::InitPicking(int width, int height)
{
	if (pickingFBO && width == pickWidth && height == pickHeight) return;

	if (pickingFBO) {
		glDeleteFramebuffers(1, &pickingFBO);
		glDeleteTextures(1, &pickingTexture);
		glDeleteRenderbuffers(1, &pickingDepth);
	}

	printf("[SceneManager] Initializing picking FBO: %d x %d (Previous: %d x %d, FBO ID: %u)\n", 
		width, height, pickWidth, pickHeight, pickingFBO);

	pickWidth = width;
	pickHeight = height;

	glGenFramebuffers(1, &pickingFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);

	glGenTextures(1, &pickingTexture);
	glBindTexture(GL_TEXTURE_2D, pickingTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickingTexture, 0);

	glGenRenderbuffers(1, &pickingDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, pickingDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickingDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("Picking framebuffer not complete!\n");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!pickingInitialized) {
		pickingShader.CreateFromFiles("Shaders/picking.vert", "Shaders/picking.frag");
		pickingInitialized = true;
	}
}

int SceneManager::PickObject(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth, float viewportHeight)
{
	if (!pickingInitialized) return -1;

	// State safety for picking pass - disable all interference
	GLint oldViewport[4];
	glGetIntegerv(GL_VIEWPORT, oldViewport);
	
	GLboolean oldBlend = glIsEnabled(GL_BLEND);
	GLboolean oldScissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean oldDepthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean oldCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean oldDither = glIsEnabled(GL_DITHER);
	GLint oldDepthFunc; glGetIntegerv(GL_DEPTH_FUNC, &oldDepthFunc);
	GLint oldCullMode; glGetIntegerv(GL_CULL_FACE_MODE, &oldCullMode);
	GLint oldPolygonMode[2]; glGetIntegerv(GL_POLYGON_MODE, oldPolygonMode);

	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DITHER);
#ifdef GL_MULTISAMPLE
	glDisable(GL_MULTISAMPLE);
#endif
#ifdef GL_FRAMEBUFFER_SRGB
	glDisable(GL_FRAMEBUFFER_SRGB);
#endif

	glBindFramebuffer(GL_FRAMEBUFFER, pickingFBO);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glViewport(0, 0, pickWidth, pickHeight);
	
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	pickingShader.UseShader();

	glUniformMatrix4fv(pickingShader.GetProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(pickingShader.GetViewLocation(), 1, GL_FALSE, glm::value_ptr(view));

	GLint modelLoc = pickingShader.GetModelLocation();
	GLint colorLoc = glGetUniformLocation(pickingShader.GetShaderID(), "pickingColor");
	GLint isBillboardLoc = glGetUniformLocation(pickingShader.GetShaderID(), "isBillboard");
	GLint worldPosLoc = glGetUniformLocation(pickingShader.GetShaderID(), "worldPos");
	GLint iconSizeLoc = glGetUniformLocation(pickingShader.GetShaderID(), "iconSize");

	// Reset uniform state: Ensure objects are NOT drawn as billboards
	glUniform1i(isBillboardLoc, 0);

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE); 
	glEnable(GL_CULL_FACE);

	// Scale mouse coordinates from viewport window space to picking FBO space
	float scaleX = (float)pickWidth / viewportWidth;
	float scaleY = (float)pickHeight / viewportHeight;
	int readX = (int)(mouseX * scaleX);
	int readY = pickHeight - (int)(mouseY * scaleY);

	// 1. OBJECTS PASS (Base Geometry)
	for (int i = 0; i < (int)objects.size(); i++)
	{
		glm::vec3 color = EncodeID(i + 1);
		glUniform3f(colorLoc, color.r, color.g, color.b);
		glm::mat4 modelMatrix = objects[i]->GetWorldMatrix();
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modelMatrix));
		if (objects[i]->GetModel()) objects[i]->GetModel()->RenderModel(-1, -1, -1, -1);
		else if (objects[i]->GetMesh()) objects[i]->GetMesh()->RenderMesh();
	}

	// Capture depth of regular objects BEFORE we clear it for icons/gizmos
	glFlush(); 
	float sceneDepth;
	glReadPixels(readX, readY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &sceneDepth);

	// 2. OVERLAY PASS (Icons, Gizmos) - Clear depth so they draw on top
	glClear(GL_DEPTH_BUFFER_BIT);

	// Light icons
	if (iconMesh)
	{
		glDisable(GL_CULL_FACE);
		glUniform1i(isBillboardLoc, 1);
		glUniform1f(iconSizeLoc, 0.7f);
		for (int i = 0; i < (int)lights.size(); i++)
		{
			glm::vec3 color = EncodeID(i + 10000);
			glUniform3f(colorLoc, color.r, color.g, color.b);
			glm::vec3* pos = lights[i]->GetPositionPtr();
			if (pos) {
				glUniform3f(worldPosLoc, pos->x, pos->y, pos->z);
				iconMesh->RenderMesh();
			}
		}
	}

	// Gizmo picking
	glm::vec3 gizmoPos;
	if (GetGizmoPosition(gizmoPos))
	{
		glm::mat4 objRot = GetSelectedRotationMatrix();
		float dist = glm::length(gizmoPos - cameraPos);
		glUniform1i(isBillboardLoc, 0);
		glDisable(GL_CULL_FACE);

		if (gizmoArrowModel) {
			float arrowScale = dist * 0.1f;
			struct { int id; glm::mat4 extraRot; } arrows[] = {
				{ 20001, glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1)) },
				{ 20002, glm::mat4(1.0f) },
				{ 20003, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)) }
			};
			for (auto& a : arrows) {
				glm::vec3 color = EncodeID(a.id);
				glUniform3f(colorLoc, color.r, color.g, color.b);
				glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * a.extraRot;
				m = glm::scale(m, glm::vec3(arrowScale));
				glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
				gizmoArrowModel->RenderModel(-1, -1, -1, -1);
			}
		}

		if (gizmoTorusModel) {
			float torusScale = dist * 0.1f * 0.6f;
			struct { int id; glm::mat4 extraRot; } tori[] = {
				{ 20004, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0,0,1)) },
				{ 20005, glm::mat4(1.0f) },
				{ 20006, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)) }
			};
			for (auto& t : tori) {
				glm::vec3 color = EncodeID(t.id);
				glUniform3f(colorLoc, color.r, color.g, color.b);
				glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * objRot * t.extraRot;
				m = glm::scale(m, glm::vec3(torusScale));
				glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
				gizmoTorusModel->RenderModel(-1, -1, -1, -1);
			}
		}
	}

	glEnable(GL_CULL_FACE);
	glFlush();
	glFinish(); // Ensure AMD driver completes FBO rendering before final read

	unsigned char pixel[3];
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(readX, readY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
	int pickedID = DecodeID(pixel);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDrawBuffer(GL_BACK);
	glReadBuffer(GL_BACK);
	glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
	
	// Restore all modified states
	if (oldBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (oldScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
#ifdef GL_MULTISAMPLE
	glEnable(GL_MULTISAMPLE);
#endif
	if (!oldDepthTest) glDisable(GL_DEPTH_TEST); else glEnable(GL_DEPTH_TEST);
	if (!oldCullFace) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
	if (oldDither) glEnable(GL_DITHER); else glDisable(GL_DITHER);
	glDepthFunc(oldDepthFunc);
	glCullFace(oldCullMode);
	glPolygonMode(GL_FRONT_AND_BACK, oldPolygonMode[0]);

	// UI PRIORITY: If we clicked a transformation handle, return immediately.
	// Gizmos are UI and should never be occluded by world objects or scatter blades.
	if (pickedID >= 20001 && pickedID <= 20006) {
		return pickedID;
	}

	// Smart Instance Extraction (Raycast)
	bool instanceWon = false;
	
	// Only try to select scatter if we didn't hit a Light Icon (10000-19999)
	if (pickedID < 10000) {
		glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view, viewportWidth, viewportHeight);
		float bestDist = FLT_MAX;
		InstancedGroup* bestGroup = nullptr;
		int bestIndex = -1;

		for (auto* group : instancedGroups) {
			int hitIndex;
			float hitDist;
			if (group->Raycast(cameraPos, rayDir, hitIndex, hitDist)) {
				if (hitDist < bestDist) {
					bestDist = hitDist;
					bestGroup = group;
					bestIndex = hitIndex;
				}
			}
		}

		if (bestGroup && bestIndex != -1) {
			// Project hit point into window space to compare against original scene depth
			glm::vec3 hitPoint = cameraPos + rayDir * bestDist;
			glm::vec4 clipSpace = projection * view * glm::vec4(hitPoint, 1.0f);
			float ndcDepth = clipSpace.z / clipSpace.w;
			float winDepth = ndcDepth * 0.5f + 0.5f;

			// If hit point is closer than the original world objects (monitor, floor, etc.)
			if (winDepth <= sceneDepth + 0.001f) {
				instanceWon = true;
				bestGroup->ExtractInstance(bestIndex, this);
				return (int)objects.size(); // newly spawned obj ID
			}
		}
	}

	// Final Selection Logic
	if (!instanceWon) {
		if (pickedID > 0 && pickedID <= (int)objects.size()) {
			SetSelectedIndex(pickedID - 1);
		}
		else if (pickedID >= 10000 && pickedID < 10000 + (int)lights.size()) {
			SetSelectedLightIndex(pickedID - 10000);
		}
		else if (pickedID < 20000) {
			// If we hit background (0) or something else below gizmos, clear selection
			ClearSelection();
		}
	}

	return pickedID;
}

// =====================================================================
// Icons
// =====================================================================

void SceneManager::InitIcons()
{
	iconShader.CreateFromFiles("Shaders/icon.vert", "Shaders/icon.frag");
	
	if (iconShader.GetShaderID() == 0) {
		printf("Icon shader failed to initialize!\n");
		return;
	}

	lightIconTexture = new Texture("Icons/Light.png");
	if (!lightIconTexture->LoadTextureA()) {
		printf("Failed to load Light.png icon!\n");
		delete lightIconTexture;
		lightIconTexture = nullptr;
		return;
	}

	CreateIconMesh();
}

void SceneManager::CreateIconMesh()
{
	unsigned int indices[] = { 0, 2, 1, 1, 2, 3 };

	GLfloat vertices[] = {
		-0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		-0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		 0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
	};

	iconMesh = new Mesh();
	iconMesh->CreateMesh(vertices, indices, 56, 6);
}

void SceneManager::RenderIcons(glm::mat4 projection, glm::mat4 view)
{
	if (!iconMesh || !lightIconTexture || iconShader.GetShaderID() == 0) return;

	while (glGetError() != GL_NO_ERROR);

	iconShader.UseShader();
	
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	
	GLint projLoc = iconShader.GetProjectionLocation();
	GLint viewLoc = iconShader.GetViewLocation();
	
	if (projLoc != -1) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	
	GLint worldPosLoc = glGetUniformLocation(iconShader.GetShaderID(), "worldPos");
	GLint iconSizeLoc = glGetUniformLocation(iconShader.GetShaderID(), "iconSize");
	GLint textureLoc = glGetUniformLocation(iconShader.GetShaderID(), "theTexture");
	GLint iconColorLoc = glGetUniformLocation(iconShader.GetShaderID(), "iconColor");
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, lightIconTexture->GetTextureID());
	
	if (textureLoc != (GLuint)-1) glUniform1i(textureLoc, 0); 
	if (iconSizeLoc != (GLuint)-1) glUniform1f(iconSizeLoc, 0.5f); 

	for (auto* light : lights)
	{
		if (!light) continue;

		glm::vec3* pos = light->GetPositionPtr();
		if (pos)
		{
			if (worldPosLoc != -1) glUniform3f(worldPosLoc, pos->x, pos->y, pos->z);
			
			glm::vec3* color = light->GetColorPtr();
			if (iconColorLoc != -1 && color) glUniform3f(iconColorLoc, color->x, color->y, color->z);
			
			iconMesh->RenderMesh();
		}
	}

	glUseProgram(0);
	glEnable(GL_CULL_FACE);
	
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("OpenGL error in RenderIcons: 0x%x\n", err);
	}
}

// =====================================================================
// Gizmo
// =====================================================================

void SceneManager::InitGizmo()
{
	gizmoShader.CreateFromFiles("Shaders/gizmo.vert", "Shaders/gizmo.frag");
	
	if (gizmoShader.GetShaderID() == 0) {
		printf("Gizmo shader failed to initialize!\n");
		return;
	}

	gizmoArrowModel = new Model();
	gizmoArrowModel->LoadModelCPU("Utils/arrow.obj");
	gizmoArrowModel->LoadModelGPU();

	gizmoTorusModel = new Model();
	gizmoTorusModel->LoadModelCPU("Utils/torus.obj");
	gizmoTorusModel->LoadModelGPU();
}

void SceneManager::RenderGizmo(glm::mat4 projection, glm::mat4 view, glm::vec3 cameraPos)
{
	if (isBoxSelecting) return; // Hide gizmo during box selection drag
	glm::vec3 gizmoPos;
	if (!GetGizmoPosition(gizmoPos)) return;
	if (!gizmoArrowModel || gizmoShader.GetShaderID() == 0) return;

	float dist = glm::length(gizmoPos - cameraPos);
	float scaleFactor = dist * 0.1f; // Restored to 0.1f for better thickness
	float torusScaleFactor = dist * 0.1f * 0.6f;

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);

	gizmoShader.UseShader();
	
	GLint projLoc = gizmoShader.GetProjectionLocation();
	GLint viewLoc = gizmoShader.GetViewLocation();
	GLint modelLoc = gizmoShader.GetModelLocation();
	GLint colorLoc = glGetUniformLocation(gizmoShader.GetShaderID(), "gizmoColor");

	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

	glm::mat4 objRot = GetSelectedRotationMatrix();

	// DRY: data-driven rendering for all 6 gizmo parts
	struct GizmoPart {
		Model* model;
		int axisID;
		glm::vec3 defaultColor;
		glm::mat4 extraRot;
		float scale;
	};

	// World-space translation arrows (ignore objRot)
	GizmoPart translationParts[] = {
		{ gizmoArrowModel, 20002, {0,1,0}, glm::mat4(1.0f), scaleFactor },
		{ gizmoArrowModel, 20001, {1,0,0}, glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1)), scaleFactor },
		{ gizmoArrowModel, 20003, {0,0,1}, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)), scaleFactor }
	};

	// Local-space rotation tori (use objRot)
	GizmoPart rotationParts[] = {
		{ gizmoTorusModel, 20004, {1,0,0}, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0,0,1)), torusScaleFactor },
		{ gizmoTorusModel, 20005, {0,1,0}, glm::mat4(1.0f), torusScaleFactor },
		{ gizmoTorusModel, 20006, {0,0,1}, glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0)), torusScaleFactor }
	};

	// Draw order: Arrows FIRST, Tori SECOND (so tori overlap)
	for (auto& part : translationParts)
	{
		if (!part.model) continue;
		glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * part.extraRot; // No objRot
		m = glm::scale(m, glm::vec3(part.scale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));

		if (activeDragAxis == part.axisID) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
		else glUniform3f(colorLoc, part.defaultColor.r, part.defaultColor.g, part.defaultColor.b);
		part.model->RenderModel(-1, -1, -1, -1);
	}

	for (auto& part : rotationParts)
	{
		if (!part.model) continue;
		glm::mat4 m = glm::translate(glm::mat4(1.0f), gizmoPos) * objRot * part.extraRot; // Uses objRot
		m = glm::scale(m, glm::vec3(part.scale));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));

		if (activeDragAxis == part.axisID) glUniform3f(colorLoc, 1.0f, 1.0f, 0.0f);
		else glUniform3f(colorLoc, part.defaultColor.r, part.defaultColor.g, part.defaultColor.b);
		part.model->RenderModel(-1, -1, -1, -1);
	}

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glUseProgram(0);
}



// =====================================================================
// Mouse/Gizmo Interaction
// =====================================================================

void SceneManager::HandleMousePress(int button, int action, float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, glm::vec3 cameraPos, float viewportWidth, float viewportHeight)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		// Hard re-sync picking FBO if window size changed (e.g. maximization or High DPI switch)
		if (viewportWidth > 0 && viewportHeight > 0) InitPicking((int)viewportWidth, (int)viewportHeight);
		
		int pickedID = PickObject(mouseX, mouseY, projection, view, cameraPos, viewportWidth, viewportHeight);
			printf("[SceneManager] Picked ID: %d (Active Selection: %s)\n", pickedID, GetSelectedName().c_str());
			
			glm::vec3 cameraForward = -glm::normalize(glm::vec3(glm::inverse(view)[2]));
			glm::vec3 rayOrigin = glm::vec3(glm::inverse(view)[3]);
			glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view, viewportWidth, viewportHeight);

			if (pickedID >= 20001 && pickedID <= 20003) {
				// === TRANSLATION ===
				activeDragAxis = pickedID;
				printf("Gizmo Drag START: Axis %d\n", activeDragAxis);
				
				// 1. Get Visual Gizmo Center for intersection
				glm::vec3 gizmoCenter;
				GetGizmoPosition(gizmoCenter);
				dragRotationCenter = gizmoCenter; // Store as a consistent interaction center

				// 2. Capture Initial States for ALL selected items
				dragInitialObjectStates.clear();
				dragInitialLightPositions.clear();

				if (!selectedObjectIndices.empty()) {
					for (int idx : selectedObjectIndices) {
						if (idx >= 0 && idx < (int)objects.size()) {
							GameObject* obj = objects[idx];
							dragInitialObjectStates[obj] = { glm::vec3(obj->GetWorldMatrix()[3]), obj->GetTransform().GetRotation() };
						}
					}
					// Anchor point for the main gizmo
					int selObj = GetSelectedIndex();
					dragInitialObjectPos = glm::vec3(objects[selObj]->GetWorldMatrix()[3]);
				}
				else if (!selectedLightIndices.empty()) {
					for (int idx : selectedLightIndices) {
						if (idx >= 0 && idx < (int)lights.size()) {
							LightObject* lo = lights[idx];
							if (lo->GetPositionPtr()) {
								dragInitialLightPositions[lo] = *lo->GetPositionPtr();
							}
						}
					}
					// Anchor point for the main gizmo
					int selLight = GetSelectedLightIndex();
					glm::vec3* lp = lights[selLight]->GetPositionPtr();
					if (lp) dragInitialObjectPos = *lp;
				}
				
				glm::vec3 axis(0.0f);
				if (activeDragAxis == 20001) axis = glm::vec3(1, 0, 0);
				else if (activeDragAxis == 20002) axis = glm::vec3(0, 1, 0);
				else axis = glm::vec3(0, 0, 1);

				
				glm::vec3 crossCamAxis = glm::cross(cameraForward, axis);
				if (glm::length(crossCamAxis) < 1e-4f) {
					glm::vec3 cameraUp = glm::normalize(glm::vec3(glm::inverse(view)[1]));
					crossCamAxis = glm::cross(cameraUp, axis);
				}
				dragPlaneNormal = glm::normalize(glm::cross(axis, crossCamAxis));
					
				RayPlaneIntersect(rayOrigin, rayDir, dragRotationCenter, dragPlaneNormal, dragInitialIntersectPos);
			}
			else if (pickedID >= 20004 && pickedID <= 20006) {
				// === ROTATION ===
				activeDragAxis = pickedID;
				printf("Gizmo Rotation START: Axis %d\n", activeDragAxis);

				GetGizmoPosition(dragRotationCenter); // Use visual center for rotation math
				
				// Capture Initial States for ALL selected objects (Lights don't rotate via gizmo yet)
				dragInitialObjectStates.clear();
				for (int idx : selectedObjectIndices) {
					if (idx >= 0 && idx < (int)objects.size()) {
						GameObject* obj = objects[idx];
						dragInitialObjectStates[obj] = { glm::vec3(obj->GetWorldMatrix()[3]), obj->GetTransform().GetRotation() };
					}
				}

				int selObj = GetSelectedIndex();
				if (selObj != -1) {
					dragInitialObjectRot = objects[selObj]->GetTransform().GetRotation();
				} else {
					dragInitialObjectRot = glm::vec3(0.0f);
				}

				// Rotation axis in world space
				glm::mat4 objRot = GetSelectedRotationMatrix();
				if (activeDragAxis == 20004) dragRotationAxis = glm::vec3(objRot * glm::vec4(1, 0, 0, 0));
				else if (activeDragAxis == 20005) dragRotationAxis = glm::vec3(objRot * glm::vec4(0, 1, 0, 0));
				else if (activeDragAxis == 20006) dragRotationAxis = glm::vec3(objRot * glm::vec4(0, 0, 1, 0));
				dragRotationAxis = glm::normalize(dragRotationAxis);

				// Rotation plane: perpendicular to the rotation axis, through center
				dragPlaneNormal = dragRotationAxis;

				glm::vec3 hitPoint;
				if (RayPlaneIntersect(rayOrigin, rayDir, dragRotationCenter, dragPlaneNormal, hitPoint)) {
					dragInitialRotVec = glm::normalize(hitPoint - dragRotationCenter);
				} else {
					// Fallback: if ray is parallel to plane, use camera right
					dragInitialRotVec = glm::normalize(glm::vec3(glm::inverse(view)[0]));
				}

				dragInitialMousePos = glm::vec2(mouseX, mouseY);
			}
		}
		else if (action == GLFW_RELEASE) {
			if (activeDragAxis != 0) {
				printf("Gizmo Drag END\n");

				// Record undo action for the completed drag
				if (activeDragAxis >= 20001 && activeDragAxis <= 20003) {
					// TRANSLATION — record transform undo for objects
					if (!dragInitialObjectStates.empty()) {
						std::vector<TransformSnapshot> before, after;
						for (auto const& [obj, state] : dragInitialObjectStates) {
							before.push_back({ obj, state.position, state.rotation, obj->GetTransform().GetScale() });
							after.push_back({ obj, obj->GetTransform().GetPosition(), obj->GetTransform().GetRotation(), obj->GetTransform().GetScale() });
						}
						undoManager.PushAction(std::make_unique<TransformAction>("Move Object", before, after));
					}
					// TRANSLATION — record light position undo
					if (!dragInitialLightPositions.empty()) {
						std::vector<LightTransformSnapshot> before, after;
						for (auto const& [light, initialPos] : dragInitialLightPositions) {
							before.push_back({ light, initialPos });
							glm::vec3* curPos = light->GetPositionPtr();
							after.push_back({ light, curPos ? *curPos : initialPos });
						}
						undoManager.PushAction(std::make_unique<LightTransformAction>("Move Light", before, after));
					}
				}
				else if (activeDragAxis >= 20004 && activeDragAxis <= 20006) {
					// ROTATION — record transform undo
					if (!dragInitialObjectStates.empty()) {
						std::vector<TransformSnapshot> before, after;
						for (auto const& [obj, state] : dragInitialObjectStates) {
							before.push_back({ obj, state.position, state.rotation, obj->GetTransform().GetScale() });
							after.push_back({ obj, obj->GetTransform().GetPosition(), obj->GetTransform().GetRotation(), obj->GetTransform().GetScale() });
						}
						undoManager.PushAction(std::make_unique<TransformAction>("Rotate Object", before, after));
					}
				}
			}
			activeDragAxis = 0;
		}
}
void SceneManager::HandleMouseMove(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight)
{
	if (activeDragAxis == 0) return;

	// Use viewport dimensions for ray casting
	glm::vec3 rayOrigin = glm::vec3(glm::inverse(view)[3]);
	glm::vec3 rayDir = GetMouseRay(mouseX, mouseY, projection, view, viewportWidth, viewportHeight);
	
	if (activeDragAxis >= 20001 && activeDragAxis <= 20003) {
		// === TRANSLATION ===
		glm::vec3 currentIntersect;
		// IMPORTANT: Must use the same dragRotationCenter (Gizmo Center) as when we started
		if (!RayPlaneIntersect(rayOrigin, rayDir, dragRotationCenter, dragPlaneNormal, currentIntersect))
			return;

		glm::vec3 delta = currentIntersect - dragInitialIntersectPos;
		
		// World-space translation arrows: ignore objRot
		glm::vec3 axis(0.0f);
		if (activeDragAxis == 20001) axis = glm::vec3(1, 0, 0);
		else if (activeDragAxis == 20002) axis = glm::vec3(0, 1, 0);
		else if (activeDragAxis == 20003) axis = glm::vec3(0, 0, 1);
		
		float movement = glm::dot(delta, axis);
		glm::vec3 worldDelta = axis * movement;

		// Move ALL selected objects
		for (auto const& [obj, state] : dragInitialObjectStates) {
			// Hierarchy Optimization: If the parent is also being moved, don't move the child directly
			// otherwise it gets double-translated.
			bool parentMoved = false;
			GameObject* p = obj->GetParent();
			while (p) {
				if (dragInitialObjectStates.count(p)) { parentMoved = true; break; }
				p = p->GetParent();
			}
			if (parentMoved) continue;

			glm::vec3 newPos = state.position + worldDelta;

			if (obj->GetParent()) {
				glm::mat4 parentWorld = obj->GetParent()->GetWorldMatrix();
				// Use unscaled parent matrix to avoid distortion if target doesn't inherit scale
				if (!obj->GetInheritScale()) {
					parentWorld[0] = glm::normalize(parentWorld[0]);
					parentWorld[1] = glm::normalize(parentWorld[1]);
					parentWorld[2] = glm::normalize(parentWorld[2]);
				}
				glm::mat4 invParent = glm::inverse(parentWorld);
				glm::vec4 localPos = invParent * glm::vec4(newPos, 1.0f);
				obj->GetTransform().SetPosition(glm::vec3(localPos));
			} else {
				obj->GetTransform().SetPosition(newPos);
			}
		}

		// Move ALL selected lights
		for (auto const& [light, initialPos] : dragInitialLightPositions) {
			light->SetPosition(initialPos + worldDelta);
		}
	}
	else if (activeDragAxis >= 20004 && activeDragAxis <= 20006) {
		// === ROTATION ===
		glm::vec3 hitPoint;
		float deltaAngle = 0.0f;

		if (!RayPlaneIntersect(rayOrigin, rayDir, dragRotationCenter, dragPlaneNormal, hitPoint)) {
			// Fallback: use mouse delta for rotation when ray is parallel to plane
			float dx = mouseX - dragInitialMousePos.x;
			deltaAngle = dx * 0.5f; 
		} else {
			glm::vec3 currentVec = hitPoint - dragRotationCenter;
			float len = glm::length(currentVec);
			if (len > 1e-6f) {
				currentVec /= len;
				// Signed angle between initial and current vectors, relative to the rotation axis
				float dotVal = glm::clamp(glm::dot(dragInitialRotVec, currentVec), -1.0f, 1.0f);
				glm::vec3 crossVal = glm::cross(dragInitialRotVec, currentVec);
				float sign = glm::dot(crossVal, dragRotationAxis);
				float angleRad = atan2(glm::length(crossVal) * (sign >= 0 ? 1.0f : -1.0f), dotVal);
				deltaAngle = glm::degrees(angleRad);
			}
		}

		glm::vec3 rotationDelta(0.0f);
		if (activeDragAxis == 20004) rotationDelta.x = deltaAngle;
		else if (activeDragAxis == 20005) rotationDelta.y = deltaAngle;
		else if (activeDragAxis == 20006) rotationDelta.z = deltaAngle;

		// Rotate ALL selected objects
		for (auto const& [obj, state] : dragInitialObjectStates) {
			// Like translation, avoid double-rotation if parent is also selected
			bool parentRotated = false;
			GameObject* p = obj->GetParent();
			while (p) {
				if (dragInitialObjectStates.count(p)) { parentRotated = true; break; }
				p = p->GetParent();
			}
			if (parentRotated) continue;

			obj->GetTransform().SetRotation(state.rotation + rotationDelta);
		}
	}
}

// =====================================================================
// Math Utilities
// =====================================================================

glm::vec3 SceneManager::GetMouseRay(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view, float viewportWidth, float viewportHeight)
{
	// Map to NDC using specific viewport dimensions
	float x = (2.0f * mouseX) / viewportWidth - 1.0f;
	float y = 1.0f - (2.0f * mouseY) / viewportHeight;
	
	glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 rayEye = glm::inverse(projection) * rayClip;
	rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
	
	glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
	return glm::normalize(rayWorld);
}

bool SceneManager::RayPlaneIntersect(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 planePoint, glm::vec3 planeNormal, glm::vec3& intersectPoint)
{
	float denom = glm::dot(planeNormal, rayDir);
	if (std::abs(denom) > 1e-6) {
		float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
		if (t >= 0) {
			intersectPoint = rayOrigin + rayDir * t;
			return true;
		}
	}
	return false;
}
