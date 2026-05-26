#define NOMINMAX
#include <windows.h>
/**
 * \mainpage Real OpenGL Project Documentation
 * 
 * \section intro_sec Introduction
 * Welcome to the Real OpenGL Project. This application is a modular, high-performance,
 * procedural world-generation and rendering engine built on C++ and OpenGL.
 * It provides advanced rendering systems, node-based procedural pipelines, and editing interfaces.
 * 
 * \section components_sec Core Components
 * - **Core**: Windowing, input handling, frustum culling, and camera controllers.
 * - **Rendering**: Forward rendering pipeline supporting instancing, tessellation, cascaded shadow mapping, SSAO, volumetric sky, and custom shaders.
 * - **Scene Management**: A tree-structured GameObject hierarchy with LOD support, scene serialization, and point/spot/directional lights.
 * - **Procedural Generation**: A comprehensive node-based mesh manipulation system (e.g. noise generators, erosion, city grid nodes, and scatter algorithms).
 * - **Editor & UI**: A modern ImGui-based editor featuring inspector panels, asset browsers, node builders, and debugging visualizations.
 * 
 * Use the navigation bar above to view the lists of classes, namespaces, and source files.
 */
// =====================================================================

// Force Dedicated GPU on Dual-GPU Laptops (NVIDIA & AMD)
// =====================================================================
extern "C" {
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

#define _CRT_SECURE_NO_WARNINGS

#ifdef RUN_UNIT_TESTS
// =====================================================================
// Unit Test Entry Point (Check Unit Test configuration)
// =====================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "External Libs/doctest.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"

int main()
{
	// Force the GPU exports to not be stripped
	volatile DWORD forceNvidia = NvOptimusEnablement;
	volatile int forceAmd = AmdPowerXpressRequestHighPerformance;
	(void)forceNvidia; (void)forceAmd;

	static AssetManager assetManager;
	ServiceLocator::Provide(&assetManager);

	// --- Boot a hidden GLFW window for an OpenGL context ---
	if (!glfwInit()) {
		printf("[Test] GLFW init failed!\n");
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hidden window

	GLFWwindow* testWindow = glfwCreateWindow(64, 64, "UnitTestContext", NULL, NULL);
	if (!testWindow) {
		printf("[Test] Failed to create hidden GLFW window!\n");
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(testWindow);

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		printf("[Test] GLEW init failed!\n");
		glfwDestroyWindow(testWindow);
		glfwTerminate();
		return -1;
	}

	// Print GPU info for test diagnostics
	const char* renderer = (const char*)glGetString(GL_RENDERER);
	const char* vendor   = (const char*)glGetString(GL_VENDOR);
	const char* version  = (const char*)glGetString(GL_VERSION);
	printf("=========================================\n");
	printf("  [TEST MODE] OpenGL Context Ready\n");
	printf("  GPU: %s\n", renderer ? renderer : "Unknown");
	printf("  Vendor: %s\n", vendor ? vendor : "Unknown");
	printf("  OpenGL: %s\n", version ? version : "Unknown");
	printf("=========================================\n\n");

	// --- Run doctest ---
	doctest::Context ctx;
	int testResult = ctx.run();

	// --- Cleanup ---
	glfwDestroyWindow(testWindow);
	glfwTerminate();

	return testResult;
}

#else
// =====================================================================
// Normal Editor Entry Point (Debug / Release configurations)
// =====================================================================
#include "Application.h"
#include <iostream>
#include <GL/glew.h>
#include "Core/AssetManager.h"
#include "Core/ServiceLocator.h"

using namespace std;

int main()
{
	// Ensure the flags are not stripped by the linker
	volatile DWORD forceNvidia = NvOptimusEnablement;
	volatile int forceAmd = AmdPowerXpressRequestHighPerformance;
	(void)forceNvidia; (void)forceAmd;

	static AssetManager assetManager;
	ServiceLocator::Provide(&assetManager);

	Application app;
	if (!app.Init()) return -1;

	// Print which GPU is actually being used
	const char* renderer = (const char*)glGetString(GL_RENDERER);
	const char* vendor   = (const char*)glGetString(GL_VENDOR);
	const char* version  = (const char*)glGetString(GL_VERSION);
	printf("=========================================\n");
	printf("  GPU: %s\n", renderer ? renderer : "Unknown");
	printf("  Vendor: %s\n", vendor ? vendor : "Unknown");
	printf("  OpenGL: %s\n", version ? version : "Unknown");
	printf("=========================================\n");

	app.Run();

	app.Shutdown();

	return 0;
}

#endif // RUN_UNIT_TESTS
