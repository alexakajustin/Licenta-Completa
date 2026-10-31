#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "Editor/DebugOverlay.h"
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
	for (auto& [name, timer] : passTimers) timer.Cleanup();

	for (auto& frame : pendingObjectFrames)
		for (auto& obj : frame) ReleaseQueries(obj);
	for (auto& obj : objectCosts) ReleaseQueries(obj);
	if (!freeTimestampQueries.empty()) glDeleteQueries((GLsizei)freeTimestampQueries.size(), freeTimestampQueries.data());
	if (!freePrimitiveQueries.empty()) glDeleteQueries((GLsizei)freePrimitiveQueries.size(), freePrimitiveQueries.data());
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

	// NOTE: Frame-level GL_TIME_ELAPSED removed — it conflicts with per-pass timers.
	// gpuTimeMs is now computed as sum of pass timers in EndFrame().

	// Reset per-pass active flags
	for (auto& [name, timer] : passTimers) timer.active = false;

	// Per-object tracking: pick up the results of earlier frames, start a new list
	CollectObjectResults();
	objectCosts.clear();
	currentObject = nullptr;
}

GLuint DebugOverlay::AcquireQuery(std::vector<GLuint>& pool)
{
	if (pool.empty()) {
		GLuint batch[64];
		glGenQueries(64, batch);
		pool.insert(pool.end(), batch, batch + 64);
	}
	GLuint q = pool.back();
	pool.pop_back();
	return q;
}

void DebugOverlay::ReleaseQueries(const ObjectCost& obj)
{
	freeTimestampQueries.push_back(obj.queries[0]);
	freeTimestampQueries.push_back(obj.queries[1]);
	freePrimitiveQueries.push_back(obj.queries[2]);
}

void DebugOverlay::CollectObjectResults()
{
	while (!pendingObjectFrames.empty()) {
		std::vector<ObjectCost>& frame = pendingObjectFrames.front();
		// The GPU runs commands in order: once the frame's last timestamp is written, every
		// query of that frame has its result. Waiting is only forced 4 frames behind.
		GLint available = 1;
		if (!frame.empty()) glGetQueryObjectiv(frame.back().queries[1], GL_QUERY_RESULT_AVAILABLE, &available);
		if (!available && pendingObjectFrames.size() < 4) break;

		std::map<std::string, float> frameMs;
		for (ObjectCost& obj : frame) {
			GLuint64 start = 0, end = 0, prims = 0;
			glGetQueryObjectui64v(obj.queries[0], GL_QUERY_RESULT, &start);
			glGetQueryObjectui64v(obj.queries[1], GL_QUERY_RESULT, &end);
			glGetQueryObjectui64v(obj.queries[2], GL_QUERY_RESULT, &prims);
			obj.gpuMs = end > start ? (float)((end - start) / 1.0e6) : 0.0f;
			obj.gpuPrimitives = prims;
			frameMs[obj.name] += obj.gpuMs;
			ReleaseQueries(obj);
		}
		// Smooth per name (an object can have several scopes per frame, e.g. two culling phases)
		for (auto& [name, ms] : frameMs) {
			auto it = smoothedObjectMs.find(name);
			if (it == smoothedObjectMs.end()) smoothedObjectMs[name] = ms;
			else it->second = it->second * 0.85f + ms * 0.15f;
		}
		lastObjectCosts = std::move(frame);
		pendingObjectFrames.pop_front();
	}
}

void DebugOverlay::EndFrame()
{
	// Compute total GPU time as sum of all pass timers
	// (Frame-level GL_TIME_ELAPSED removed — it conflicted with per-pass queries)
	float totalGpu = 0.0f;
	for (auto& [name, timer] : passTimers) {
		if (timer.active) totalGpu += timer.smoothedMs;
	}
	gpuTimeMs = totalGpu;

	// Snapshot counters
	lastDrawCalls = drawCallCount;
	lastTriangles = triangleCount;

	// This frame's object queries are read back a few frames later (CollectObjectResults)
	if (currentObject) EndObject();
	if (!objectCosts.empty()) pendingObjectFrames.push_back(std::move(objectCosts));
	objectCosts.clear();
}

void DebugOverlay::ResetCounters()
{
	drawCallCount = 0;
	triangleCount = 0;
}

// --- Per-pass profiling ---
void DebugOverlay::BeginPass(const std::string& passName)
{
	auto it = passTimers.find(passName);
	if (it == passTimers.end()) {
		PassTimer timer;
		timer.name = passName;
		timer.Init();
		passTimers[passName] = timer;
		passOrder.push_back(passName);
		it = passTimers.find(passName);
	}
	it->second.Begin();
}

void DebugOverlay::EndPass(const std::string& passName)
{
	auto it = passTimers.find(passName);
	if (it != passTimers.end()) {
		it->second.End();
	}
}

// --- Per-object cost tracking ---
void DebugOverlay::BeginObject(const std::string& name, int meshCount)
{
	if (currentObject) EndObject(); // Scopes never nest (one primitives query at a time)
	if (!panelVisible) return;      // Measured only while the breakdown is on screen

	ObjectCost cost;
	cost.name = name;
	cost.meshCount = meshCount;
	cost.queries[0] = AcquireQuery(freeTimestampQueries);
	cost.queries[1] = AcquireQuery(freeTimestampQueries);
	cost.queries[2] = AcquireQuery(freePrimitiveQueries);
	glQueryCounter(cost.queries[0], GL_TIMESTAMP);
	glBeginQuery(GL_PRIMITIVES_GENERATED, cost.queries[2]);
	objectCosts.push_back(cost);
	currentObject = &objectCosts.back();
}

void DebugOverlay::CountObjectDrawCall()
{
	if (currentObject) currentObject->drawCalls++;
}

void DebugOverlay::CountObjectTriangles(int count)
{
	if (currentObject) currentObject->triangles += count;
}

void DebugOverlay::EndObject()
{
	if (currentObject) {
		glEndQuery(GL_PRIMITIVES_GENERATED);
		glQueryCounter(currentObject->queries[1], GL_TIMESTAMP);
	}
	currentObject = nullptr;
}

void DebugOverlay::Render(EditorUI::WindowState& uiState)
{
	panelVisible = false; // Set again by RenderObjectBreakdown while the table is on screen
	if (!uiState.isDebugOverlayOpen) return;

	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	float winWidth = displaySize.x;
	float winHeight = displaySize.y;
	float menuHeight = 19.0f;
	
	// CPU Debug on bottom-right
	ImVec2 pos(winWidth - uiState.rightWidth, menuHeight + (winHeight - menuHeight) * uiState.rightHeightRatio);
	ImVec2 size(uiState.rightWidth, (winHeight - menuHeight) * (1.0f - uiState.rightHeightRatio));

	if (uiState.maximizedWindowID == 5) { // Debug Maximized
		pos = ImVec2(0, menuHeight);
		size = ImVec2(winWidth, winHeight - menuHeight);
	} else if (uiState.maximizedWindowID != -1) { // Something ELSE maximized
		return;
	}

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.95f);

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
	
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	bool debugOpen = ImGui::Begin("CPU Debug", &uiState.isDebugOverlayOpen, windowFlags);
	ImGui::PopStyleVar();

	if (debugOpen)
	{
		uiState.CheckMaximize(5);

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

	// --- Render Pass GPU Timers ---
	RenderPassTimers();

	ImGui::Spacing();

	// --- Draw Stats ---
	ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.6f, 1.0f), "Draw Stats");
	ImGui::Separator();
	ImGui::Text("Draw Calls: %d (all passes)", lastDrawCalls);
	ImGui::Text("Triangles:  %d", lastTriangles);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Regular meshes in all passes (shadows included). GPU-driven instanced\ngroups are counted per object in the breakdown below.");

	ImGui::Spacing();

	// --- Per-Object Breakdown ---
	RenderObjectBreakdown();

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
	}

	ImGui::End();
}

void DebugOverlay::RenderPassTimers()
{
	if (passTimers.empty()) return;

	ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Render Pass Profiler");
	ImGui::Separator();

	// Collect total GPU time across passes
	float totalPassTime = 0.0f;
	for (auto& name : passOrder) {
		auto& timer = passTimers[name];
		if (timer.active) totalPassTime += timer.smoothedMs;
	}

	// Render bar chart
	float availWidth = ImGui::GetContentRegionAvail().x;

	for (auto& name : passOrder) {
		auto& timer = passTimers[name];
		if (!timer.active) continue;

		float fraction = (totalPassTime > 0.0f) ? (timer.smoothedMs / totalPassTime) : 0.0f;

		// Color: green = fast, yellow = moderate, red = slow
		ImVec4 barColor;
		if (timer.smoothedMs < 2.0f) barColor = ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
		else if (timer.smoothedMs < 8.0f) barColor = ImVec4(0.9f, 0.8f, 0.2f, 1.0f);
		else barColor = ImVec4(1.0f, 0.3f, 0.2f, 1.0f);

		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
		char overlayText[64];
		snprintf(overlayText, sizeof(overlayText), "%s: %.2f ms (%.0f%%)", name.c_str(), timer.smoothedMs, fraction * 100.0f);
		ImGui::ProgressBar(fraction, ImVec2(availWidth, 18), overlayText);
		ImGui::PopStyleColor();
	}

	ImGui::Text("Total Pass Time: %.2f ms", totalPassTime);
}

void DebugOverlay::RenderObjectBreakdown()
{
	ImGui::TextColored(ImVec4(0.8f, 0.5f, 1.0f, 1.0f), "Object Cost Breakdown");
	
	ImGui::SameLine();
	ImGui::Checkbox("Show##ObjBreakdown", &showObjectBreakdown);

	if (!showObjectBreakdown) return;
	panelVisible = true;

	ImGui::Separator();
	ImGui::TextDisabled("Main view only: GPU time and drawn triangles per object,");
	ImGui::TextDisabled("instanced groups [GPU] and engine work [in brackets] included");

	if (lastObjectCosts.empty()) {
		ImGui::TextDisabled("Measuring...");
		return;
	}

	// One row per name: an object can have several scopes per frame (two culling phases)
	struct Row {
		std::string name;
		float ms = 0.0f;
		GLuint64 tris = 0;
		int draws = 0;
	};
	std::map<std::string, Row> merged;
	for (const ObjectCost& obj : lastObjectCosts) {
		Row& row = merged[obj.name];
		row.name = obj.name;
		row.tris += obj.gpuPrimitives > 0 ? obj.gpuPrimitives : (GLuint64)obj.triangles;
		row.draws += obj.drawCalls;
	}
	std::vector<Row> rows;
	float totalMs = 0.0f;
	GLuint64 totalTris = 0;
	for (auto& [name, row] : merged) {
		auto it = smoothedObjectMs.find(name);
		row.ms = it != smoothedObjectMs.end() ? it->second : 0.0f;
		totalMs += row.ms;
		totalTris += row.tris;
		rows.push_back(row);
	}

	// Sort options
	ImGui::Text("Sort:");
	ImGui::SameLine();
	if (ImGui::SmallButton("GPU")) objectSortMode = 0;
	ImGui::SameLine();
	if (ImGui::SmallButton("Tris")) objectSortMode = 1;
	ImGui::SameLine();
	if (ImGui::SmallButton("Draws")) objectSortMode = 2;
	ImGui::SameLine();
	if (ImGui::SmallButton("Name")) objectSortMode = 3;

	if (objectSortMode == 0)
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
	else if (objectSortMode == 1)
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.tris > b.tris; });
	else if (objectSortMode == 2)
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.draws > b.draws; });
	else
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.name < b.name; });

	// Table
	if (ImGui::BeginTable("##ObjCostTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 240))) {
		ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 52);
		ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, 56);
		ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 40);
		ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 32);
		ImGui::TableHeadersRow();

		// Show top 50 to avoid flooding
		int shown = 0;
		for (const Row& row : rows) {
			if (shown >= 50) break;

			float pct = (totalMs > 0.0f) ? (row.ms * 100.0f / totalMs) : 0.0f;

			ImGui::TableNextRow();

			// Highlight expensive objects
			if (pct > 10.0f) {
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.5f, 0.1f, 0.1f, 0.4f)));
			}

			ImGui::TableSetColumnIndex(0);
			// Truncate long names
			std::string displayName = row.name;
			if (displayName.length() > 32) displayName = displayName.substr(0, 29) + "...";
			ImGui::TextUnformatted(displayName.c_str());
			if (ImGui::IsItemHovered() && displayName != row.name) ImGui::SetTooltip("%s", row.name.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.2f", row.ms);

			ImGui::TableSetColumnIndex(2);
			if (row.tris >= 100000)
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%lluk", (unsigned long long)(row.tris / 1000));
			else
				ImGui::Text("%llu", (unsigned long long)row.tris);

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", row.draws);

			ImGui::TableSetColumnIndex(4);
			if (pct > 10.0f)
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%.0f", pct);
			else
				ImGui::Text("%.0f", pct);

			shown++;
		}

		ImGui::EndTable();
	}

	ImGui::Text("%d rows | %.2f ms GPU | %.2fM tris", (int)rows.size(), totalMs, totalTris / 1.0e6);
	auto mainPass = passTimers.find("Main Render");
	if (mainPass != passTimers.end() && mainPass->second.active) {
		float other = std::max(mainPass->second.smoothedMs - totalMs, 0.0f);
		ImGui::TextDisabled("Main Render %.2f ms = rows %.2f + state changes/gaps %.2f", mainPass->second.smoothedMs, totalMs, other);
	}
}

void DebugOverlay::RenderMemoryInfo()
{
	// --- Process RAM (32-bit limit awareness) ---
	ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "System RAM (Process)");
	ImGui::Separator();
	
	PROCESS_MEMORY_COUNTERS_EX pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
	{
		size_t usedBytes = pmc.PrivateUsage;
		float usedMB = usedBytes / (1024.0f * 1024.0f);

		// Query actual system RAM (not a hardcoded 4GB guess)
		MEMORYSTATUSEX memStatus;
		memStatus.dwLength = sizeof(memStatus);
		float totalMB = 16384.0f; // Fallback
		float availMB = 0.0f;
		if (GlobalMemoryStatusEx(&memStatus)) {
			totalMB = memStatus.ullTotalPhys / (1024.0f * 1024.0f);
			availMB = memStatus.ullAvailPhys / (1024.0f * 1024.0f);
		}
		float usagePercent = usedMB / totalMB;
		
		ImVec4 ramColor = usagePercent < 0.50f ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) :
						  usagePercent < 0.80f ? ImVec4(1.0f, 1.0f, 0.2f, 1.0f) :
												 ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
		
		ImGui::TextColored(ramColor, "Process: %.0f MB", usedMB);
		ImGui::Text("System:  %.0f / %.0f MB (%.0f%% free)", totalMB - availMB, totalMB, (availMB / totalMB) * 100.0f);
		
		char overlay[48];
		snprintf(overlay, sizeof(overlay), "%.0f MB / %.0f GB", usedMB, totalMB / 1024.0f);
		ImGui::ProgressBar(usagePercent, ImVec2(0, 18), overlay);
	}
	else {
		ImGui::TextDisabled("Could not query process memory info.");
	}

	ImGui::Spacing();

	ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "VRAM (GPU)");
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
