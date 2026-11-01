#include "Nodes/OutputNode.h"
#include "Scene/SceneManager.h"
#include "Scene/GameObject.h"
#include "Rendering/Mesh.h"
#include <glm/gtc/matrix_transform.hpp>

OutputNode::OutputNode(NodeGraph& graph) : GraphNode(graph)
{
	AddInput(PinType::Mesh, "Mesh", "mesh");
	paramPins = false;
	AddBool("sameAsInput", "Same as input", &sameAsInput).Tip("Write into the object the mesh was read from");
	AddObjects("target", "Target", &target, true).Legacy("targetName").ShowIf([this] { return !sameAsInput; });
	AddBool("updateMesh", "Write the mesh", &updateMesh).Tip("Off: the graph runs but the object keeps its mesh");
}

void OutputNode::Execute(NodeContext& ctx)
{
	const MeshValue& mesh = In("mesh").mesh;
	std::string targetName = sameAsInput ? mesh.sourceObject : (target.empty() ? std::string() : target[0]);
	if (targetName.empty()) {
		if (sameAsInput && In("mesh").Valid()) ctx.Warn("The mesh was not read from an object: pick a target");
		return;
	}
	GameObject* obj = ctx.scene.FindObject(targetName);
	if (!obj) { ctx.Warn("Target '" + targetName + "' not found"); return; }
	if (!updateMesh || mesh.Empty()) return;

	MeshData upload = mesh.Data();

	// Take the source object's rotation and scale back out of the vertices: the object keeps
	// them in its transform (no double scaling)
	if (mesh.scale != glm::vec3(1.0f) || mesh.rotation != glm::vec3(0.0f)) {
		glm::mat4 m(1.0f);
		m = glm::rotate(m, glm::radians(mesh.rotation.y), glm::vec3(0, 1, 0));
		m = glm::rotate(m, glm::radians(mesh.rotation.x), glm::vec3(1, 0, 0));
		m = glm::rotate(m, glm::radians(mesh.rotation.z), glm::vec3(0, 0, 1));
		m = glm::scale(m, mesh.scale);
		glm::mat4 inv = glm::inverse(m);
		glm::mat3 invNormal = glm::transpose(glm::mat3(m)); // Inverse of transpose(inverse(M))
		for (size_t v = 0; v < upload.vertices.size(); v += 14) {
			glm::vec4 p = inv * glm::vec4(upload.vertices[v], upload.vertices[v + 1], upload.vertices[v + 2], 1.0f);
			upload.vertices[v] = p.x; upload.vertices[v + 1] = p.y; upload.vertices[v + 2] = p.z;
			glm::vec3 n(upload.vertices[v + 5], upload.vertices[v + 6], upload.vertices[v + 7]);
			if (glm::length(n) > 0.0001f) {
				n = glm::normalize(invNormal * n);
				upload.vertices[v + 5] = n.x; upload.vertices[v + 6] = n.y; upload.vertices[v + 7] = n.z;
			}
		}
	}

	// Overwrite the object's own mesh in place, or give it a new one when it shares it
	bool shared = false;
	if (obj->GetMesh()) {
		std::vector<GameObject*> all;
		ctx.scene.GetAllObjects(all);
		for (auto* other : all)
			if (other != obj && other->GetMesh() == obj->GetMesh()) { shared = true; break; }
	}
	glm::vec3 bmin, bmax;
	upload.GetBounds(bmin, bmax);
	if (obj->GetMesh() && !shared) {
		obj->GetMesh()->CreateMesh(upload.vertices.data(), upload.indices.data(), (unsigned int)upload.vertices.size(), (unsigned int)upload.indices.size());
		obj->GetMesh()->SetBounds(bmin, bmax);
	} else {
		Mesh* newMesh = upload.ToMesh();
		obj->SetMesh(newMesh);
		if (newMesh) newMesh->SetBounds(bmin, bmax);
	}
	if (!mesh.sourceObject.empty()) {
		obj->GetTransform().SetScale(mesh.scale);
		obj->GetTransform().SetRotation(mesh.rotation);
	}
	obj->SetCPUMeshData(upload);

	if (mesh.look.material) obj->SetMaterial(mesh.look.material);
	if (mesh.look.texture) obj->SetTexture(mesh.look.texture);
	if (mesh.look.normalMap) obj->SetNormalMap(mesh.look.normalMap);
	ctx.Info("-> " + targetName);
}
