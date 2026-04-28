#include <windows.h>
// =====================================================================
// Force Dedicated GPU on Dual-GPU Laptops (NVIDIA & AMD)
// =====================================================================
extern "C" {
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

#define _CRT_SECURE_NO_WARNINGS
#include "Application.h"
#include <iostream>
#include <GL/glew.h>

using namespace std;

int main()
{
	// Ensure the flags are not stripped by the linker
	volatile DWORD forceNvidia = NvOptimusEnablement;
	volatile int forceAmd = AmdPowerXpressRequestHighPerformance;
	(void)forceNvidia; (void)forceAmd;

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
