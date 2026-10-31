#pragma once

#include <GL/glew.h>
#include <string>
#include <deque>
#include <vector>
#include <map>
#include <glm/glm.hpp>

#include "Editor/EditorUI.h"

// Per-pass GPU timer (double-buffered async queries)
struct PassTimer {
	std::string name;
	GLuint queries[2] = { 0, 0 };
	int currentQuery = 0;
	bool queryReady = false;
	float timeMs = 0.0f;
	float smoothedMs = 0.0f; // EMA smoothed
	bool active = false;     // Was this pass timed this frame?

	void Init() {
		if (!queries[0]) glGenQueries(2, queries);
	}
	void Begin() {
		active = true;
		// Read previous result
		if (queryReady) {
			GLuint64 gpuTime = 0;
			GLint available = 0;
			glGetQueryObjectiv(queries[1 - currentQuery], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available) {
				glGetQueryObjectui64v(queries[1 - currentQuery], GL_QUERY_RESULT, &gpuTime);
				timeMs = gpuTime / 1000000.0f;
				smoothedMs = smoothedMs * 0.85f + timeMs * 0.15f; // EMA
			}
		}
		glBeginQuery(GL_TIME_ELAPSED, queries[currentQuery]);
	}
	void End() {
		glEndQuery(GL_TIME_ELAPSED);
		currentQuery = 1 - currentQuery;
		queryReady = true;
	}
	void Cleanup() {
		if (queries[0]) { glDeleteQueries(2, queries); queries[0] = queries[1] = 0; }
	}
};

// Per-object render cost in the main view. GPU time and primitive count come from async
// queries (timestamps + GL_PRIMITIVES_GENERATED) read a few frames later, so measuring never
// stalls the pipeline; GPU-driven instanced groups are counted like any other object.
struct ObjectCost {
	std::string name;
	int drawCalls = 0;
	int triangles = 0;                 // Counted on the CPU (regular meshes only)
	int meshCount = 0;
	GLuint queries[3] = { 0, 0, 0 };   // Start timestamp, end timestamp, primitives generated
	float gpuMs = 0.0f;
	GLuint64 gpuPrimitives = 0;
};

class DebugOverlay
{
public:
	DebugOverlay();
	~DebugOverlay();

	// Call at start/end of frame to measure GPU time
	void BeginFrame();
	void EndFrame();

	// --- Per-pass profiling ---
	void BeginPass(const std::string& passName);
	void EndPass(const std::string& passName);

	// --- Per-object cost tracking (main view only; scopes must not nest) ---
	void BeginObject(const std::string& name, int meshCount);
	void CountObjectDrawCall();
	void CountObjectTriangles(int count);
	void EndObject();

	// Call inside ImGui frame
	void Render(EditorUI::WindowState& uiState);

	// Global access
	static DebugOverlay* GetInstance() { return instance; }

	// Track draw calls manually (call in render loop)
	void ResetCounters();
	void CountDrawCall() { drawCallCount++; }
	void CountTriangles(int count) { triangleCount += count; }

	// Set debug info
	void SetCameraInfo(glm::vec3 pos, glm::vec3 front) { camPos = pos; camFront = front; }
	void SetSceneInfo(int objCount, int lightCount) { objectCount = objCount; sceneLightCount = lightCount; }
	void SetSelectionInfo(const std::string& name) { selectedName = name; }
	void SetViewportInfo(int w, int h) { viewWidth = w; viewHeight = h; }

	bool IsOpen() const { return isOpen; }

	// Object breakdown without the UI (headless profiling): the editor turns measuring on
	// itself while the table is visible
	void SetObjectProfiling(bool enabled) { panelVisible = enabled; }
	const std::vector<ObjectCost>& GetObjectCosts() const { return lastObjectCosts; }
	const std::map<std::string, float>& GetSmoothedObjectMs() const { return smoothedObjectMs; }
	float GetPassMs(const std::string& passName) const {
		auto it = passTimers.find(passName);
		return it != passTimers.end() ? it->second.smoothedMs : 0.0f;
	}

private:
	static DebugOverlay* instance;
	bool isOpen = true;

	// Camera & Scene info
	glm::vec3 camPos = glm::vec3(0.0f);
	glm::vec3 camFront = glm::vec3(0.0f);
	int objectCount = 0;
	int sceneLightCount = 0;
	std::string selectedName = "None";
	int viewWidth = 0, viewHeight = 0;

	// Frame timing
	float deltaTime = 0.0f;
	float fps = 0.0f;
	float lastFrameTime = 0.0f;

	// FPS history for graph
	std::deque<float> fpsHistory;
	std::deque<float> frameTimeHistory;
	static const int HISTORY_SIZE = 120;

	// GPU timer query (async) — total frame
	GLuint gpuTimerQuery[2];
	int currentQuery = 0;
	bool queryReady = false;
	float gpuTimeMs = 0.0f;

	// Counters
	int drawCallCount = 0;
	int triangleCount = 0;
	int lastDrawCalls = 0;
	int lastTriangles = 0;

	// Per-pass timers
	std::map<std::string, PassTimer> passTimers;
	std::vector<std::string> passOrder; // Insertion order

	// Per-object costs: this frame's scopes (queries in flight), frames waiting for their
	// query results, and the latest frame with results
	std::vector<ObjectCost> objectCosts;
	std::deque<std::vector<ObjectCost>> pendingObjectFrames;
	std::vector<ObjectCost> lastObjectCosts;
	std::map<std::string, float> smoothedObjectMs; // EMA per object name
	// Query object pools: a query keeps the type of its first use, so timestamp and
	// primitives queries are never mixed
	std::vector<GLuint> freeTimestampQueries;
	std::vector<GLuint> freePrimitiveQueries;
	ObjectCost* currentObject = nullptr;
	bool showObjectBreakdown = false;
	bool panelVisible = false;                     // Breakdown on screen last frame: only then measure
	int objectSortMode = 0; // 0=GPU time, 1=triangles, 2=drawcalls, 3=name

	static GLuint AcquireQuery(std::vector<GLuint>& pool);
	void ReleaseQueries(const ObjectCost& obj);
	void CollectObjectResults();

	// GPU info (cached at init)
	std::string gpuVendor;
	std::string gpuRenderer;
	std::string glVersion;
	bool hasNvidiaMemInfo = false;
	bool hasAtiMemInfo = false;

	void QueryGPUInfo();
	void RenderMemoryInfo();
	void RenderPassTimers();
	void RenderObjectBreakdown();
};
