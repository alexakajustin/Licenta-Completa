#include "Application.h"
#include <iostream>
#include <GL/glew.h>

// =====================================================================
// Force Dedicated GPU on Dual-GPU Laptops
// =====================================================================
// NVIDIA Optimus: Setting this to 1 tells the driver to use the
// dedicated NVIDIA GPU instead of the integrated Intel GPU.
extern "C" {
	__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}

// AMD PowerXpress / Enduro: Same concept for AMD dedicated GPUs.
extern "C" {
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

using namespace std;

int main()
{
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
