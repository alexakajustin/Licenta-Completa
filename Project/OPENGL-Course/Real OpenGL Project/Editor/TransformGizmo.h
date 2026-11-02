#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

struct ImDrawList;

// The editor's move / rotate / scale gizmo. Pure screen-space math and ImGui drawing: it knows
// nothing about the scene. Each frame the owner gives it the camera (SetView) and the selection's
// pivot and orientation (SetTarget); a drag yields a translation, rotation or scale relative to
// where it started, which the owner applies to its own start states.
//
// Handles are hit-tested analytically in pixels (constant tolerance at any distance and zoom),
// the gizmo keeps a constant size on screen, the handle under the mouse lights up, axes seen
// end-on fade out instead of jumping, and holding Ctrl inverts snapping during a drag.
class TransformGizmo
{
public:
	enum class Mode { Translate, Rotate, Scale };
	enum class Space { World, Local };   // Scale always works in local axes
	enum class Pivot { Center, Origin }; // Bounds center of the selection, or the active object's origin

	enum Handle {
		None = 0,
		MoveX, MoveY, MoveZ, MoveYZ, MoveXZ, MoveXY, MoveScreen,
		RotX, RotY, RotZ, RotScreen,
		ScaleX, ScaleY, ScaleZ, ScaleUniform
	};

	Mode mode = Mode::Translate;
	Space space = Space::World;
	Pivot pivot = Pivot::Center;
	bool snap = false;          // Ctrl during a drag inverts it
	float snapMove = 0.5f;      // World units
	float snapAngle = 15.0f;    // Degrees
	float snapScale = 0.1f;     // Scale factor steps
	float size = 110.0f;        // Length of an axis on screen (pixels)

	// --- Each frame, before HitTest / Begin / Draw ---
	void SetView(const glm::mat4& view, const glm::mat4& projection, glm::vec2 viewportSize);
	// orientation: world rotation of the active object (its local axes)
	void SetTarget(const glm::vec3& pivotPos, const glm::mat3& orientation, bool canRotate, bool canScale);
	void ClearTarget() { hasTarget = false; }
	bool HasTarget() const { return hasTarget; }
	Mode EffectiveMode() const; // Translate when the selection cannot rotate / scale

	// --- Interaction (mouse in viewport pixels, origin top-left) ---
	Handle HitTest(glm::vec2 mouse) const;
	void SetHovered(Handle h) { hovered = h; }
	bool Begin(glm::vec2 mouse);                // Grabs the handle under the mouse
	bool Update(glm::vec2 mouse, bool invertSnap); // True when the result changed
	void End();
	bool IsDragging() const { return active != None; }
	Handle GetActive() const { return active; }
	Handle GetHovered() const { return hovered; }
	static bool IsMove(Handle h) { return h >= MoveX && h <= MoveScreen; }
	static bool IsRotate(Handle h) { return h >= RotX && h <= RotScreen; }
	static bool IsScale(Handle h) { return h >= ScaleX && h <= ScaleUniform; }

	// --- Result of the current drag, relative to its start ---
	glm::vec3 GetTranslation() const { return translation; }
	glm::quat GetRotation() const { return rotation; }
	glm::vec3 GetScale() const { return scale; }         // Along GetDragAxes()
	glm::vec3 GetDragPivot() const { return startPivot; }
	glm::mat3 GetDragAxes() const { return startAxes; }

	void Draw(ImDrawList* drawList, glm::vec2 viewportScreenPos) const;

	// Projection helpers (viewport pixels)
	bool WorldToScreen(const glm::vec3& p, glm::vec2& out) const;
	void MouseRay(glm::vec2 mouse, glm::vec3& origin, glm::vec3& dir) const;

private:
	// Camera
	glm::mat4 view = glm::mat4(1.0f), proj = glm::mat4(1.0f), viewProj = glm::mat4(1.0f), invViewProj = glm::mat4(1.0f);
	glm::vec2 viewportSize = glm::vec2(1.0f);
	glm::vec3 camPos = glm::vec3(0.0f), camForward = glm::vec3(0, 0, -1), camRight = glm::vec3(1, 0, 0), camUp = glm::vec3(0, 1, 0);
	bool ortho = false;

	// Target
	bool hasTarget = false, canRotate = true, canScale = true;
	glm::vec3 targetPivot = glm::vec3(0.0f);
	glm::mat3 targetOrientation = glm::mat3(1.0f);

	Handle hovered = None, active = None;

	// Drag
	glm::vec2 startMouse = glm::vec2(0.0f);
	glm::vec3 startPivot = glm::vec3(0.0f);
	glm::mat3 startAxes = glm::mat3(1.0f);
	float startLength = 1.0f;          // World length of an axis at the start
	glm::vec3 startHit = glm::vec3(0.0f);
	float startParam = 0.0f;           // Along the axis (axis moves)
	glm::vec3 rotAxis = glm::vec3(0, 1, 0);
	bool rotOnPlane = true;            // Follow the cursor on the ring's plane, or drag along its tangent
	glm::vec3 rotStartVec = glm::vec3(1, 0, 0), rotLastVec = glm::vec3(1, 0, 0);
	glm::vec2 rotTangent = glm::vec2(1, 0);
	float rotLastScreenAngle = 0.0f;
	float rotAccum = 0.0f;             // Radians, unwrapped (several turns are fine)
	float rotShown = 0.0f;             // Degrees after snapping
	glm::vec2 scaleDir = glm::vec2(1, 0);

	glm::vec3 translation = glm::vec3(0.0f);
	glm::quat rotation = glm::quat(1, 0, 0, 0);
	glm::vec3 scale = glm::vec3(1.0f);
	std::string readout;

	// Current pivot and axes (follow the drag)
	glm::vec3 DisplayPivot() const;
	glm::mat3 DisplayAxes() const;
	glm::mat3 AxesFor(Mode m) const;
	float WorldPerPixel(const glm::vec3& p) const;
	float AxisVisibility(const glm::vec3& pivotPos, const glm::vec3& dir, float length) const;
	float PlaneVisibility(const glm::vec3& pivotPos, const glm::vec3& normal) const;
	glm::vec3 PlaneSide(const glm::vec3& pivotPos, const glm::mat3& axes) const;
	bool RayPlane(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& n, glm::vec3& hit) const;
	bool AxisParam(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& axis, float& t) const;
	float ScreenAngle(glm::vec2 mouse) const;
};
