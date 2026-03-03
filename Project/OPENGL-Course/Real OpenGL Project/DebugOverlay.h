#pragma once

#include <GL/glew.h>
#include <string>
#include <deque>
#include <glm/glm.hpp>

class DebugOverlay
{
public:
	DebugOverlay();
	~DebugOverlay();

	// Call at start/end of frame to measure GPU time
	void BeginFrame();
	void EndFrame();

	// Call inside ImGui frame
	void Render();

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

	// GPU timer query (async)
	GLuint gpuTimerQuery[2];
	int currentQuery = 0;
	bool queryReady = false;
	float gpuTimeMs = 0.0f;

	// Counters
	int drawCallCount = 0;
	int triangleCount = 0;
	int lastDrawCalls = 0;
	int lastTriangles = 0;

	// GPU info (cached at init)
	std::string gpuVendor;
	std::string gpuRenderer;
	std::string glVersion;
	bool hasNvidiaMemInfo = false;
	bool hasAtiMemInfo = false;

	void QueryGPUInfo();
	void RenderMemoryInfo();
};
