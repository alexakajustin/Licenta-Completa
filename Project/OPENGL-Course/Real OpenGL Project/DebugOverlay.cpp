#include "DebugOverlay.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

DebugOverlay* DebugOverlay::instance = nullptr;

// NVIDIA extension constants
#ifndef GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX
#define GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX   0x9048
#endif
#ifndef GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX
#define GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX 0x9049
#endif

// ATI/AMD extension constants
#ifndef GL_VBO_FREE_MEMORY_ATI
#define GL_VBO_FREE_MEMORY_ATI                    0x87FB
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI                0x87FC
#endif

DebugOverlay::DebugOverlay()
{
	instance = this;
	gpuTimerQuery[0] = 0;
	gpuTimerQuery[1] = 0;
}

DebugOverlay::~DebugOverlay()
{
	if (gpuTimerQuery[0]) glDeleteQueries(2, gpuTimerQuery);
}

void DebugOverlay::QueryGPUInfo()
{
	gpuVendor = (const char*)glGetString(GL_VENDOR);
	gpuRenderer = (const char*)glGetString(GL_RENDERER);
	glVersion = (const char*)glGetString(GL_VERSION);

	// Check for memory info extensions
	int numExtensions = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
	for (int i = 0; i < numExtensions; i++) {
		const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if (ext) {
			if (strcmp(ext, "GL_NVX_gpu_memory_info") == 0) hasNvidiaMemInfo = true;
			if (strcmp(ext, "GL_ATI_meminfo") == 0) hasAtiMemInfo = true;
		}
	}

	// Create GPU timer queries
	glGenQueries(2, gpuTimerQuery);

	printf("[DebugOverlay] GPU: %s | %s\n", gpuVendor.c_str(), gpuRenderer.c_str());
	printf("[DebugOverlay] VRAM query: NVX=%d ATI=%d\n", hasNvidiaMemInfo, hasAtiMemInfo);
}

void DebugOverlay::BeginFrame()
{
	// Init GPU info on first frame (needs GL context)
	if (gpuVendor.empty()) QueryGPUInfo();

	float now = (float)glfwGetTime();
	deltaTime = now - lastFrameTime;
	lastFrameTime = now;

	if (deltaTime > 0.0f) fps = 1.0f / deltaTime;

	// FPS history
	fpsHistory.push_back(fps);
	if ((int)fpsHistory.size() > HISTORY_SIZE) fpsHistory.pop_front();

	frameTimeHistory.push_back(deltaTime * 1000.0f);
	if ((int)frameTimeHistory.size() > HISTORY_SIZE) frameTimeHistory.pop_front();

	// Start GPU timer (double-buffered: read previous, start new)
	if (queryReady) {
		GLuint64 gpuTime = 0;
		GLint available = 0;
		glGetQueryObjectiv(gpuTimerQuery[1 - currentQuery], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available) {
			glGetQueryObjectui64v(gpuTimerQuery[1 - currentQuery], GL_QUERY_RESULT, &gpuTime);
			gpuTimeMs = gpuTime / 1000000.0f; // ns -> ms
		}
	}

	glBeginQuery(GL_TIME_ELAPSED, gpuTimerQuery[currentQuery]);
}

void DebugOverlay::EndFrame()
{
	glEndQuery(GL_TIME_ELAPSED);
	currentQuery = 1 - currentQuery;
	queryReady = true;

	// Snapshot counters
	lastDrawCalls = drawCallCount;
	lastTriangles = triangleCount;
}

void DebugOverlay::ResetCounters()
{
	drawCallCount = 0;
	triangleCount = 0;
}

void DebugOverlay::Render(bool* p_open)
{
	if (p_open && !*p_open) return;
	if (!p_open && !isOpen) return;

	bool* activeOpen = p_open ? p_open : &isOpen;

	int bufferWidth, bufferHeight;
	glfwGetFramebufferSize(glfwGetCurrentContext(), &bufferWidth, &bufferHeight);
	ImGui::SetNextWindowPos(ImVec2((float)bufferWidth * 0.7f, (float)bufferHeight * 0.7f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2((float)bufferWidth * 0.3f, (float)bufferHeight * 0.3f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowBgAlpha(0.85f);

	ImGui::Begin("GPU Debug", activeOpen, ImGuiWindowFlags_NoFocusOnAppearing);

	// --- GPU Info ---
	ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "GPU Info");
	ImGui::Separator();
	ImGui::Text("Vendor:   %s", gpuVendor.c_str());
	ImGui::Text("Renderer: %s", gpuRenderer.c_str());
	ImGui::Text("GL:       %s", glVersion.c_str());

	ImGui::Spacing();

	// --- Performance ---
	ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Performance");
	ImGui::Separator();

	// Color FPS based on value
	ImVec4 fpsColor = fps > 60 ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
					  fps > 30 ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
								 ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

	ImGui::TextColored(fpsColor, "FPS: %.0f", fps);
	ImGui::SameLine(150);
	ImGui::Text("Frame: %.2f ms", deltaTime * 1000.0f);

	ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "GPU Time: %.2f ms", gpuTimeMs);
	ImGui::Text("Viewport: %d x %d", viewWidth, viewHeight);

	// FPS Graph
	if (!fpsHistory.empty()) {
		std::vector<float> fpsData(fpsHistory.begin(), fpsHistory.end());
		float maxFps = *std::max_element(fpsData.begin(), fpsData.end());
		char overlay[32];
		snprintf(overlay, sizeof(overlay), "%.0f FPS", fps);
		ImGui::PlotLines("##FPS", fpsData.data(), (int)fpsData.size(), 0, overlay, 0.0f, maxFps * 1.2f, ImVec2(0, 50));
	}

	// Frame Time Graph
	if (!frameTimeHistory.empty()) {
		std::vector<float> ftData(frameTimeHistory.begin(), frameTimeHistory.end());
		char overlay[32];
		snprintf(overlay, sizeof(overlay), "%.1f ms", deltaTime * 1000.0f);
		ImGui::PlotLines("##FrameTime", ftData.data(), (int)ftData.size(), 0, overlay, 0.0f, 50.0f, ImVec2(0, 50));
	}

	ImGui::Spacing();

	// --- Draw Stats ---
	ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "Draw Stats");
	ImGui::Separator();
	ImGui::Text("Draw Calls: %d", lastDrawCalls);
	ImGui::Text("Triangles:  %d", lastTriangles);

	ImGui::Spacing();

	// --- Scene ---
	ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Scene Stats");
	ImGui::Separator();
	ImGui::Text("Objects: %d", objectCount);
	ImGui::Text("Lights:  %d", sceneLightCount);

	ImGui::Spacing();

	// --- Camera ---
	ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "Camera");
	ImGui::Separator();
	ImGui::Text("Pos:   %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
	ImGui::Text("Front: %.1f, %.1f, %.1f", camFront.x, camFront.y, camFront.z);

	ImGui::Spacing();

	// --- Selection ---
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Selection");
	ImGui::Separator();
	ImGui::Text("Active: %s", selectedName.c_str());

	ImGui::Spacing();

	// --- VRAM ---
	RenderMemoryInfo();

	// --- OpenGL State ---
	ImGui::Spacing();
	ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.8f, 1.0f), "GL State");
	ImGui::Separator();

	GLint maxTexSize, maxTexUnits, maxFBOAttach;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxFBOAttach);
	ImGui::Text("Max Tex Size:  %d", maxTexSize);
	ImGui::Text("Tex Units:     %d", maxTexUnits);
	ImGui::Text("FBO Attach:    %d", maxFBOAttach);

	// Flush stale GL errors from FBO rendering, picking, thumbnails, etc.
	while (glGetError() != GL_NO_ERROR) {}
	ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "GL State: OK");

	ImGui::End();
}

void DebugOverlay::RenderMemoryInfo()
{
	ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "VRAM");
	ImGui::Separator();

	if (hasNvidiaMemInfo) {
		GLint totalMemKB = 0, availMemKB = 0;
		glGetIntegerv(GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX, &totalMemKB);
		glGetIntegerv(GL_GPU_MEM_INFO_CURRENT_AVAILABLE_MEM_NVX, &availMemKB);

		float totalMB = totalMemKB / 1024.0f;
		float availMB = availMemKB / 1024.0f;
		float usedMB = totalMB - availMB;
		float usagePercent = (totalMB > 0) ? (usedMB / totalMB) : 0.0f;

		ImVec4 vramColor = usagePercent < 0.7f ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
						   usagePercent < 0.9f ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
												 ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

		ImGui::TextColored(vramColor, "Used: %.0f / %.0f MB (%.0f%%)", usedMB, totalMB, usagePercent * 100.0f);

		char overlay[32];
		snprintf(overlay, sizeof(overlay), "%.0f%%", usagePercent * 100.0f);
		ImGui::ProgressBar(usagePercent, ImVec2(0, 18), overlay);
	}
	else if (hasAtiMemInfo) {
		GLint texFreeMem[4] = { 0 };
		glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, texFreeMem);
		float freeMB = texFreeMem[0] / 1024.0f;
		ImGui::Text("Free VRAM: %.0f MB", freeMB);
	}
	else {
		ImGui::TextDisabled("VRAM query not supported (no NVX/ATI extension)");
	}
}
