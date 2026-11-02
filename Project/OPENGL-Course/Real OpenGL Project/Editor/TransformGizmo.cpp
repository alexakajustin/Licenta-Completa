#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "Editor/TransformGizmo.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
	const float PI = 3.14159265358979f;
	const float HIT_TOLERANCE = 8.0f;     // Pixels around a handle that still grab it
	const float RING_RADIUS = 0.9f;       // Of the axis length
	const float SCREEN_RING = 1.08f;      // View-axis ring, of the axis length (on screen)
	const float PLANE_MIN = 0.22f, PLANE_MAX = 0.46f; // Plane handle square, of the axis length
	const int RING_SEGMENTS = 72;

	const ImU32 AXIS_COLORS[3] = { IM_COL32(232, 68, 72, 255), IM_COL32(122, 204, 58, 255), IM_COL32(58, 132, 240, 255) };
	const ImU32 HOT_COLOR = IM_COL32(255, 214, 10, 255);

	ImU32 WithAlpha(ImU32 c, float a)
	{
		int alpha = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f));
		return (c & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
	}

	float SmoothStep(float e0, float e1, float x)
	{
		float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	float DistToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b)
	{
		glm::vec2 ab = b - a;
		float len2 = glm::dot(ab, ab);
		float t = len2 > 1e-6f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
		return glm::length(p - (a + ab * t));
	}

	bool InConvexQuad(glm::vec2 p, const glm::vec2 q[4])
	{
		int sign = 0;
		for (int i = 0; i < 4; i++) {
			glm::vec2 e = q[(i + 1) % 4] - q[i], d = p - q[i];
			float c = e.x * d.y - e.y * d.x;
			int s = c > 0.0f ? 1 : (c < 0.0f ? -1 : 0);
			if (s == 0) continue;
			if (sign == 0) sign = s;
			else if (s != sign) return false;
		}
		return true;
	}

	glm::vec3 RingPoint(const glm::mat3& axes, int k, float angle)
	{
		return axes[(k + 1) % 3] * std::cos(angle) + axes[(k + 2) % 3] * std::sin(angle);
	}
}

// =====================================================================
// Setup
// =====================================================================

void TransformGizmo::SetView(const glm::mat4& v, const glm::mat4& p, glm::vec2 size2)
{
	view = v;
	proj = p;
	viewProj = p * v;
	invViewProj = glm::inverse(viewProj);
	viewportSize = glm::max(size2, glm::vec2(1.0f));
	glm::mat4 inv = glm::inverse(v);
	camPos = glm::vec3(inv[3]);
	camRight = glm::normalize(glm::vec3(inv[0]));
	camUp = glm::normalize(glm::vec3(inv[1]));
	camForward = -glm::normalize(glm::vec3(inv[2]));
	ortho = std::abs(p[3][3] - 1.0f) < 1e-4f;
}

void TransformGizmo::SetTarget(const glm::vec3& pivotPos, const glm::mat3& orientation, bool rotate, bool scaleAllowed)
{
	hasTarget = true;
	targetPivot = pivotPos;
	// Orthonormal: the object's scale (even negative) must not skew the handles
	glm::vec3 x = orientation[0], y = orientation[1];
	if (glm::length(x) < 1e-6f || glm::length(y) < 1e-6f) { targetOrientation = glm::mat3(1.0f); }
	else {
		x = glm::normalize(x);
		glm::vec3 z = glm::cross(x, y);
		if (glm::length(z) < 1e-6f) { targetOrientation = glm::mat3(1.0f); }
		else {
			z = glm::normalize(z);
			y = glm::cross(z, x);
			targetOrientation = glm::mat3(x, y, z);
		}
	}
	canRotate = rotate;
	canScale = scaleAllowed;
}

TransformGizmo::Mode TransformGizmo::EffectiveMode() const
{
	if (mode == Mode::Rotate && !canRotate) return Mode::Translate;
	if (mode == Mode::Scale && !canScale) return Mode::Translate;
	return mode;
}

glm::mat3 TransformGizmo::AxesFor(Mode m) const
{
	return (m == Mode::Scale || space == Space::Local) ? targetOrientation : glm::mat3(1.0f);
}

glm::vec3 TransformGizmo::DisplayPivot() const
{
	if (IsMove(active)) return startPivot + translation;
	if (active != None) return startPivot;
	return targetPivot;
}

glm::mat3 TransformGizmo::DisplayAxes() const
{
	if (active == None) return AxesFor(EffectiveMode());
	if (IsRotate(active) && space == Space::Local) return glm::mat3_cast(rotation) * startAxes;
	return startAxes;
}

// =====================================================================
// Projection helpers
// =====================================================================

bool TransformGizmo::WorldToScreen(const glm::vec3& p, glm::vec2& out) const
{
	glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
	if (clip.w <= 1e-5f) return false;
	glm::vec3 ndc = glm::vec3(clip) / clip.w;
	out = glm::vec2((ndc.x * 0.5f + 0.5f) * viewportSize.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y);
	return true;
}

void TransformGizmo::MouseRay(glm::vec2 mouse, glm::vec3& origin, glm::vec3& dir) const
{
	float x = 2.0f * mouse.x / viewportSize.x - 1.0f;
	float y = 1.0f - 2.0f * mouse.y / viewportSize.y;
	glm::vec4 n = invViewProj * glm::vec4(x, y, -1.0f, 1.0f);
	glm::vec4 f = invViewProj * glm::vec4(x, y, 1.0f, 1.0f);
	glm::vec3 nearP = glm::vec3(n) / n.w, farP = glm::vec3(f) / f.w;
	origin = ortho ? nearP : camPos;
	dir = glm::normalize(farP - nearP);
}

float TransformGizmo::WorldPerPixel(const glm::vec3& p) const
{
	float k = 2.0f / (std::max(std::abs(proj[1][1]), 1e-6f) * viewportSize.y);
	if (ortho) return k;
	float depth = -(view * glm::vec4(p, 1.0f)).z;
	return std::max(depth, 1e-3f) * k;
}

// How much of an axis is left on screen: none when it points at the camera
float TransformGizmo::AxisVisibility(const glm::vec3& p, const glm::vec3& dir, float length) const
{
	glm::vec2 a, b;
	if (!WorldToScreen(p, a) || !WorldToScreen(p + dir * length, b)) return 0.0f;
	return SmoothStep(0.12f, 0.25f, glm::length(b - a) / size);
}

// A plane handle seen edge-on cannot be dragged sensibly
float TransformGizmo::PlaneVisibility(const glm::vec3& p, const glm::vec3& normal) const
{
	glm::vec3 toCam = ortho ? -camForward : glm::normalize(camPos - p);
	return SmoothStep(0.12f, 0.3f, std::abs(glm::dot(normal, toCam)));
}

// Plane handles sit in the quadrant facing the camera, so they never hide behind the pivot
glm::vec3 TransformGizmo::PlaneSide(const glm::vec3& p, const glm::mat3& axes) const
{
	glm::vec3 toCam = ortho ? -camForward : camPos - p;
	glm::vec3 s;
	for (int i = 0; i < 3; i++) s[i] = glm::dot(axes[i], toCam) >= 0.0f ? 1.0f : -1.0f;
	return s;
}

bool TransformGizmo::RayPlane(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& n, glm::vec3& hit) const
{
	float denom = glm::dot(n, d);
	if (std::abs(denom) < 1e-6f) return false;
	float t = glm::dot(p - o, n) / denom;
	if (t < 0.0f) return false;
	hit = o + d * t;
	return true;
}

// Parameter along the axis line (through p) of the point closest to the mouse ray
bool TransformGizmo::AxisParam(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p, const glm::vec3& axis, float& t) const
{
	glm::vec3 w0 = p - o;
	float b = glm::dot(axis, d);
	float denom = 1.0f - b * b;
	if (denom < 1e-5f) return false; // Ray along the axis
	float dd = glm::dot(axis, w0), e = glm::dot(d, w0);
	float rayT = (e - b * dd) / denom;
	if (rayT < 0.0f) return false;   // Closest point behind the camera
	t = (b * e - dd) / denom;
	return true;
}

float TransformGizmo::ScreenAngle(glm::vec2 mouse) const
{
	glm::vec2 c;
	if (!WorldToScreen(startPivot, c)) return 0.0f;
	return std::atan2(-(mouse.y - c.y), mouse.x - c.x);
}

// =====================================================================
// Hit testing
// =====================================================================

TransformGizmo::Handle TransformGizmo::HitTest(glm::vec2 mouse) const
{
	if (!hasTarget) return None;
	Mode m = EffectiveMode();
	glm::mat3 axes = AxesFor(m);
	glm::vec3 P = targetPivot;
	glm::vec2 pc;
	if (!WorldToScreen(P, pc)) return None;
	float L = size * WorldPerPixel(P);

	if (m == Mode::Translate) {
		if (glm::length(mouse - pc) <= HIT_TOLERANCE) return MoveScreen;

		float best = HIT_TOLERANCE + 1.0f;
		int bestAxis = -1;
		for (int i = 0; i < 3; i++) {
			if (AxisVisibility(P, axes[i], L) < 0.3f) continue;
			glm::vec2 a, b;
			if (!WorldToScreen(P + axes[i] * (0.15f * L), a) || !WorldToScreen(P + axes[i] * L, b)) continue;
			float d = DistToSegment(mouse, a, b);
			if (d < best) { best = d; bestAxis = i; }
		}
		if (bestAxis >= 0 && best <= HIT_TOLERANCE) return (Handle)(MoveX + bestAxis);

		glm::vec3 side = PlaneSide(P, axes);
		for (int k = 0; k < 3; k++) {
			if (PlaneVisibility(P, axes[k]) < 0.3f) continue;
			int i = (k + 1) % 3, j = (k + 2) % 3;
			glm::vec3 u = axes[i] * side[i] * L, v = axes[j] * side[j] * L;
			glm::vec3 corners[4] = { P + u * PLANE_MIN + v * PLANE_MIN, P + u * PLANE_MAX + v * PLANE_MIN,
									 P + u * PLANE_MAX + v * PLANE_MAX, P + u * PLANE_MIN + v * PLANE_MAX };
			glm::vec2 q[4];
			bool ok = true;
			for (int c = 0; c < 4; c++) ok = ok && WorldToScreen(corners[c], q[c]);
			if (ok && InConvexQuad(mouse, q)) return (Handle)(MoveYZ + k);
		}
		return None;
	}

	if (m == Mode::Rotate) {
		float best = HIT_TOLERANCE + 1.0f;
		Handle bestHandle = None;
		glm::vec3 toCam = ortho ? -camForward : glm::normalize(camPos - P);
		float R = RING_RADIUS * L;
		for (int k = 0; k < 3; k++) {
			glm::vec2 prev;
			bool prevOk = false;
			glm::vec3 prevW;
			for (int s = 0; s <= RING_SEGMENTS; s++) {
				glm::vec3 w = P + RingPoint(axes, k, 2.0f * PI * s / RING_SEGMENTS) * R;
				glm::vec2 sp;
				bool ok = WorldToScreen(w, sp);
				if (ok && prevOk) {
					glm::vec3 mid = (w + prevW) * 0.5f - P;
					// Only the half of the ring facing the camera (the rest is behind the gizmo)
					if (glm::dot(glm::normalize(mid), toCam) > -0.05f) {
						float d = DistToSegment(mouse, prev, sp);
						if (d < best) { best = d; bestHandle = (Handle)(RotX + k); }
					}
				}
				prev = sp; prevOk = ok; prevW = w;
			}
		}
		float ring = std::abs(glm::length(mouse - pc) - SCREEN_RING * size);
		if (ring < best) { best = ring; bestHandle = RotScreen; }
		return best <= HIT_TOLERANCE ? bestHandle : None;
	}

	// Scale
	glm::vec2 dc = glm::abs(mouse - pc);
	if (std::max(dc.x, dc.y) <= HIT_TOLERANCE) return ScaleUniform;
	float best = HIT_TOLERANCE + 1.0f;
	int bestAxis = -1;
	for (int i = 0; i < 3; i++) {
		if (AxisVisibility(P, axes[i], L) < 0.3f) continue;
		glm::vec2 a, b;
		if (!WorldToScreen(P + axes[i] * (0.15f * L), a) || !WorldToScreen(P + axes[i] * (0.85f * L), b)) continue;
		float d = DistToSegment(mouse, a, b);
		glm::vec2 db = glm::abs(mouse - b);
		if (std::max(db.x, db.y) <= HIT_TOLERANCE) d = 0.0f; // The cube at the end
		if (d < best) { best = d; bestAxis = i; }
	}
	return (bestAxis >= 0 && best <= HIT_TOLERANCE) ? (Handle)(ScaleX + bestAxis) : None;
}

// =====================================================================
// Dragging
// =====================================================================

bool TransformGizmo::Begin(glm::vec2 mouse)
{
	Handle h = HitTest(mouse);
	if (h == None) return false;

	startMouse = mouse;
	startPivot = targetPivot;
	startAxes = AxesFor(EffectiveMode());
	startLength = size * WorldPerPixel(startPivot);
	translation = glm::vec3(0.0f);
	rotation = glm::quat(1, 0, 0, 0);
	scale = glm::vec3(1.0f);
	rotAccum = 0.0f;
	rotShown = 0.0f;
	readout.clear();

	glm::vec3 o, d;
	MouseRay(mouse, o, d);
	glm::vec3 toCam = ortho ? -camForward : glm::normalize(camPos - startPivot);

	if (h >= MoveX && h <= MoveZ) {
		if (!AxisParam(o, d, startPivot, startAxes[h - MoveX], startParam)) return false;
	}
	else if (h >= MoveYZ && h <= MoveXY) {
		if (!RayPlane(o, d, startPivot, startAxes[h - MoveYZ], startHit)) return false;
	}
	else if (h == MoveScreen) {
		if (!RayPlane(o, d, startPivot, camForward, startHit)) return false;
	}
	else if (h >= RotX && h <= RotZ) {
		int k = h - RotX;
		rotAxis = startAxes[k];
		float R = RING_RADIUS * startLength;
		// The ring point under the mouse: where the drag starts (and the swept angle is drawn from)
		float bestD = 1e30f, bestA = 0.0f;
		for (int s = 0; s < RING_SEGMENTS * 2; s++) {
			float a = PI * s / RING_SEGMENTS;
			glm::vec2 sp;
			if (!WorldToScreen(startPivot + RingPoint(startAxes, k, a) * R, sp)) continue;
			float dd = glm::length(sp - mouse);
			if (dd < bestD) { bestD = dd; bestA = a; }
		}
		rotStartVec = RingPoint(startAxes, k, bestA);
		glm::vec3 hit;
		// Facing the camera: follow the cursor around the ring. Edge-on: the plane is useless,
		// drag along the ring's tangent on screen instead.
		rotOnPlane = std::abs(glm::dot(rotAxis, toCam)) > 0.2f && RayPlane(o, d, startPivot, rotAxis, hit) &&
			glm::length(hit - startPivot) > 1e-4f * startLength;
		if (rotOnPlane) rotLastVec = glm::normalize(hit - startPivot);
		glm::vec3 grab = startPivot + rotStartVec * R;
		glm::vec2 g0, g1;
		glm::vec3 tangent = glm::cross(rotAxis, rotStartVec);
		if (WorldToScreen(grab, g0) && WorldToScreen(grab + tangent * (0.1f * startLength), g1) && glm::length(g1 - g0) > 1e-3f)
			rotTangent = glm::normalize(g1 - g0);
		else
			rotTangent = glm::vec2(1.0f, 0.0f);
	}
	else if (h == RotScreen) {
		rotAxis = toCam;
		rotLastScreenAngle = ScreenAngle(mouse);
		rotStartVec = camRight * std::cos(rotLastScreenAngle) + camUp * std::sin(rotLastScreenAngle);
	}
	else if (h >= ScaleX && h <= ScaleZ) {
		glm::vec2 s0, s1;
		if (WorldToScreen(startPivot, s0) && WorldToScreen(startPivot + startAxes[h - ScaleX] * startLength, s1) && glm::length(s1 - s0) > 1e-3f)
			scaleDir = glm::normalize(s1 - s0);
		else
			scaleDir = glm::vec2(1.0f, 0.0f);
	}

	active = h;
	hovered = h;
	return true;
}

bool TransformGizmo::Update(glm::vec2 mouse, bool invertSnap)
{
	if (active == None) return false;
	bool snapOn = snap != invertSnap;
	glm::vec3 o, d;
	MouseRay(mouse, o, d);
	glm::vec3 oldT = translation, oldS = scale;
	glm::quat oldR = rotation;
	char buf[96];
	auto Snap = [](float v, float step) { return step > 0.0f ? std::round(v / step) * step : v; };
	float limit = startLength * 5000.0f; // A ray grazing the plane / axis: ignore the jump

	if (active >= MoveX && active <= MoveZ) {
		int i = active - MoveX;
		float t;
		if (!AxisParam(o, d, startPivot, startAxes[i], t)) return false;
		float delta = t - startParam;
		if (std::abs(delta) > limit) return false;
		if (snapOn) delta = Snap(delta, snapMove);
		translation = startAxes[i] * delta;
		snprintf(buf, sizeof(buf), "%c  %+.3f", "XYZ"[i], delta);
		readout = buf;
	}
	else if (IsMove(active)) {
		glm::vec3 n = active == MoveScreen ? camForward : startAxes[active - MoveYZ];
		glm::vec3 hit;
		if (!RayPlane(o, d, startPivot, n, hit)) return false;
		glm::vec3 delta = hit - startHit;
		if (glm::length(delta) > limit) return false;
		glm::vec3 c = glm::transpose(startAxes) * delta; // Along the gizmo's axes
		if (active != MoveScreen) c[active - MoveYZ] = 0.0f;
		if (snapOn) for (int k = 0; k < 3; k++) c[k] = Snap(c[k], snapMove);
		translation = startAxes * c;
		snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", c.x, c.y, c.z);
		readout = buf;
	}
	else if (IsRotate(active)) {
		if (active == RotScreen) {
			float a = ScreenAngle(mouse);
			float delta = a - rotLastScreenAngle;
			while (delta > PI) delta -= 2.0f * PI;
			while (delta < -PI) delta += 2.0f * PI;
			rotAccum += delta;
			rotLastScreenAngle = a;
		}
		else if (rotOnPlane) {
			glm::vec3 hit;
			if (RayPlane(o, d, startPivot, rotAxis, hit)) {
				glm::vec3 v = hit - startPivot;
				v -= rotAxis * glm::dot(v, rotAxis);
				if (glm::length(v) > 1e-4f * startLength) {
					v = glm::normalize(v);
					// Unwrapped: the angle keeps growing past a half turn instead of flipping sign
					rotAccum += std::atan2(glm::dot(glm::cross(rotLastVec, v), rotAxis), glm::clamp(glm::dot(rotLastVec, v), -1.0f, 1.0f));
					rotLastVec = v;
				}
			}
		}
		else {
			rotAccum = glm::dot(mouse - startMouse, rotTangent) / (RING_RADIUS * size);
		}
		float deg = glm::degrees(rotAccum);
		if (snapOn) deg = Snap(deg, snapAngle);
		rotShown = deg;
		rotation = glm::angleAxis(glm::radians(deg), rotAxis);
		snprintf(buf, sizeof(buf), "%+.1f\xC2\xB0", deg);
		readout = buf;
	}
	else {
		glm::vec2 dm = mouse - startMouse;
		float s = active == ScaleUniform ? 1.0f + (dm.x - dm.y) / size : 1.0f + glm::dot(dm, scaleDir) / size;
		if (snapOn) s = Snap(s, snapScale);
		s = std::max(s, 0.01f);
		if (active == ScaleUniform) {
			scale = glm::vec3(s);
			snprintf(buf, sizeof(buf), "\xC3\x97%.3f", s);
		}
		else {
			scale = glm::vec3(1.0f);
			scale[active - ScaleX] = s;
			snprintf(buf, sizeof(buf), "%c  \xC3\x97%.3f", "XYZ"[active - ScaleX], s);
		}
		readout = buf;
	}
	return translation != oldT || scale != oldS || rotation != oldR;
}

void TransformGizmo::End()
{
	active = None;
	readout.clear();
}

// =====================================================================
// Drawing
// =====================================================================

void TransformGizmo::Draw(ImDrawList* dl, glm::vec2 origin) const
{
	if (!hasTarget || !dl) return;
	const glm::vec3 P = DisplayPivot();
	const glm::mat3 axes = DisplayAxes();
	Mode m = active == None ? EffectiveMode() : (IsMove(active) ? Mode::Translate : IsRotate(active) ? Mode::Rotate : Mode::Scale);
	const float L = size * WorldPerPixel(P);
	const bool dragging = active != None;
	const ImVec2 o(origin.x, origin.y);
	glm::vec2 pc2;
	if (!WorldToScreen(P, pc2)) return;
	const ImVec2 pc = o + ImVec2(pc2.x, pc2.y);

	auto Screen = [&](const glm::vec3& w, ImVec2& out) {
		glm::vec2 s;
		if (!WorldToScreen(w, s)) return false;
		out = o + ImVec2(s.x, s.y);
		return true;
	};
	// A world-space line cut at the near plane (a guide line can reach behind the camera)
	auto Line3D = [&](glm::vec3 a, glm::vec3 b, ImU32 col, float th) {
		glm::vec4 va = view * glm::vec4(a, 1.0f), vb = view * glm::vec4(b, 1.0f);
		const float nearZ = -0.05f;
		if (va.z > nearZ && vb.z > nearZ) return;
		if (va.z > nearZ) va = vb + (va - vb) * ((nearZ - vb.z) / (va.z - vb.z));
		if (vb.z > nearZ) vb = va + (vb - va) * ((nearZ - va.z) / (vb.z - va.z));
		glm::mat4 invView = glm::inverse(view);
		ImVec2 sa, sb;
		if (Screen(glm::vec3(invView * va), sa) && Screen(glm::vec3(invView * vb), sb)) dl->AddLine(sa, sb, col, th);
	};
	auto Hot = [&](Handle h) { return active == h || (!dragging && hovered == h); };
	auto Fade = [&](Handle h) { return dragging && active != h ? 0.25f : 1.0f; };

	if (m == Mode::Translate) {
		if (active >= MoveX && active <= MoveZ) {
			glm::vec3 a = startAxes[active - MoveX];
			Line3D(startPivot - a * (L * 400.0f), startPivot + a * (L * 400.0f), WithAlpha(AXIS_COLORS[active - MoveX], 0.45f), 1.0f);
		}
		// Planes behind the arrows
		glm::vec3 side = PlaneSide(P, axes);
		for (int k = 0; k < 3; k++) {
			Handle h = (Handle)(MoveYZ + k);
			float vis = PlaneVisibility(P, axes[k]) * Fade(h);
			if (vis <= 0.01f) continue;
			int i = (k + 1) % 3, j = (k + 2) % 3;
			glm::vec3 u = axes[i] * side[i] * L, v = axes[j] * side[j] * L;
			ImVec2 q[4];
			if (!Screen(P + u * PLANE_MIN + v * PLANE_MIN, q[0]) || !Screen(P + u * PLANE_MAX + v * PLANE_MIN, q[1]) ||
				!Screen(P + u * PLANE_MAX + v * PLANE_MAX, q[2]) || !Screen(P + u * PLANE_MIN + v * PLANE_MAX, q[3])) continue;
			ImU32 col = Hot(h) ? HOT_COLOR : AXIS_COLORS[k];
			dl->AddQuadFilled(q[0], q[1], q[2], q[3], WithAlpha(col, (Hot(h) ? 0.55f : 0.3f) * vis));
			dl->AddQuad(q[0], q[1], q[2], q[3], WithAlpha(col, 0.9f * vis), 1.5f);
		}
		// Arrows, farthest first
		int order[3] = { 0, 1, 2 };
		float depth[3];
		for (int i = 0; i < 3; i++) depth[i] = (view * glm::vec4(P + axes[i] * L, 1.0f)).z;
		std::sort(order, order + 3, [&](int a, int b) { return depth[a] < depth[b]; });
		for (int i : order) {
			Handle h = (Handle)(MoveX + i);
			float vis = AxisVisibility(P, axes[i], L) * Fade(h);
			if (vis <= 0.01f) continue;
			ImVec2 a, b, tip;
			if (!Screen(P + axes[i] * (0.15f * L), a) || !Screen(P + axes[i] * (0.78f * L), b) || !Screen(P + axes[i] * L, tip)) continue;
			ImU32 col = WithAlpha(Hot(h) ? HOT_COLOR : AXIS_COLORS[i], vis);
			dl->AddLine(a, b, col, Hot(h) ? 4.0f : 3.0f);
			ImVec2 dir = tip - b;
			float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
			if (len > 1e-3f) {
				dir = dir / len;
				ImVec2 perp(-dir.y, dir.x);
				float w = Hot(h) ? 7.5f : 6.5f;
				dl->AddTriangleFilled(tip, b + perp * w, b - perp * w, col);
			}
		}
		bool hot = Hot(MoveScreen);
		float r = hot ? 6.0f : 5.0f;
		dl->AddRectFilled(pc - ImVec2(r, r), pc + ImVec2(r, r), WithAlpha(hot ? HOT_COLOR : IM_COL32(240, 240, 240, 255), 0.9f * Fade(MoveScreen)));
		dl->AddRect(pc - ImVec2(r, r), pc + ImVec2(r, r), IM_COL32(20, 20, 20, (int)(160 * Fade(MoveScreen))), 0.0f, 0, 1.0f);
	}
	else if (m == Mode::Rotate) {
		glm::vec3 toCam = ortho ? -camForward : glm::normalize(camPos - P);
		float R = RING_RADIUS * L;
		dl->AddCircleFilled(pc, RING_RADIUS * size, IM_COL32(255, 255, 255, dragging ? 8 : 14), 64);

		// Swept angle
		if (dragging && std::abs(rotShown) > 0.01f) {
			float sweep = std::clamp(glm::radians(rotShown), -2.0f * PI, 2.0f * PI);
			int n = std::clamp((int)(std::abs(sweep) / (2.0f * PI) * 96.0f), 2, 96);
			ImU32 fill = WithAlpha(HOT_COLOR, 0.22f), edge = WithAlpha(HOT_COLOR, 0.8f);
			ImVec2 first, prev, cur;
			bool prevOk = false, firstOk = false;
			for (int s = 0; s <= n; s++) {
				float a = sweep * s / n;
				bool ok;
				if (active == RotScreen) {
					float sa = rotLastScreenAngle - rotAccum + a; // Start angle on screen
					cur = pc + ImVec2(std::cos(sa), -std::sin(sa)) * (SCREEN_RING * size);
					ok = true;
				}
				else {
					ok = Screen(startPivot + (glm::angleAxis(a, rotAxis) * rotStartVec) * R, cur);
				}
				if (ok && prevOk) dl->AddTriangleFilled(pc, prev, cur, fill);
				if (ok && !firstOk) { first = cur; firstOk = true; }
				prev = cur; prevOk = ok;
			}
			if (firstOk) dl->AddLine(pc, first, edge, 1.5f);
			if (prevOk) dl->AddLine(pc, prev, edge, 2.0f);
		}

		// Rings: the active / hovered one last so it stays on top
		int order[3] = { 0, 1, 2 };
		for (int k = 0; k < 3; k++) if (Hot((Handle)(RotX + k))) std::swap(order[k], order[2]);
		for (int k : order) {
			Handle h = (Handle)(RotX + k);
			bool hot = Hot(h);
			float fade = Fade(h);
			ImU32 front = WithAlpha(hot ? HOT_COLOR : AXIS_COLORS[k], fade);
			ImU32 back = WithAlpha(AXIS_COLORS[k], 0.18f * fade);
			ImVec2 prev;
			bool prevOk = false;
			glm::vec3 prevW;
			for (int s = 0; s <= RING_SEGMENTS; s++) {
				glm::vec3 w = P + RingPoint(axes, k, 2.0f * PI * s / RING_SEGMENTS) * R;
				ImVec2 sp;
				bool ok = Screen(w, sp);
				if (ok && prevOk) {
					bool facing = glm::dot(glm::normalize((w + prevW) * 0.5f - P), toCam) > -0.05f;
					if (facing) dl->AddLine(prev, sp, front, hot ? 3.5f : 2.5f);
					else if (!dragging) dl->AddLine(prev, sp, back, 1.2f);
				}
				prev = sp; prevOk = ok; prevW = w;
			}
		}
		bool hot = Hot(RotScreen);
		dl->AddCircle(pc, SCREEN_RING * size, WithAlpha(hot ? HOT_COLOR : IM_COL32(235, 235, 235, 255), (hot ? 1.0f : 0.55f) * Fade(RotScreen)), 72, hot ? 2.5f : 1.5f);
	}
	else {
		for (int i = 0; i < 3; i++) {
			Handle h = (Handle)(ScaleX + i);
			float vis = AxisVisibility(P, axes[i], L) * Fade(h);
			if (vis <= 0.01f) continue;
			// The axis stretches with the drag
			float stretch = dragging && (active == h || active == ScaleUniform) ? std::clamp(scale[i], 0.15f, 4.0f) : 1.0f;
			ImVec2 a, b;
			if (!Screen(P + axes[i] * (0.15f * L), a) || !Screen(P + axes[i] * (0.85f * L * stretch), b)) continue;
			ImU32 col = WithAlpha(Hot(h) ? HOT_COLOR : AXIS_COLORS[i], vis);
			dl->AddLine(a, b, col, Hot(h) ? 4.0f : 3.0f);
			float r = Hot(h) ? 6.0f : 5.0f;
			dl->AddRectFilled(b - ImVec2(r, r), b + ImVec2(r, r), col);
		}
		bool hot = Hot(ScaleUniform);
		float r = (hot ? 7.0f : 6.0f) * (active == ScaleUniform ? std::clamp(scale.x, 0.5f, 2.5f) : 1.0f);
		dl->AddRectFilled(pc - ImVec2(r, r), pc + ImVec2(r, r), WithAlpha(hot ? HOT_COLOR : IM_COL32(240, 240, 240, 255), 0.9f * Fade(ScaleUniform)));
		dl->AddRect(pc - ImVec2(r, r), pc + ImVec2(r, r), IM_COL32(20, 20, 20, (int)(160 * Fade(ScaleUniform))), 0.0f, 0, 1.0f);
	}

	// What the drag did so far, next to the cursor
	if (dragging && !readout.empty()) {
		ImVec2 mp = ImGui::GetIO().MousePos + ImVec2(18.0f, 16.0f);
		ImVec2 ts = ImGui::CalcTextSize(readout.c_str());
		dl->AddRectFilled(mp - ImVec2(6, 3), mp + ts + ImVec2(6, 3), IM_COL32(16, 16, 20, 200), 4.0f);
		dl->AddText(mp, IM_COL32(255, 255, 255, 240), readout.c_str());
	}
}
