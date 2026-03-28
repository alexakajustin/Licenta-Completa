#include "Application.h"
#include <iostream>
using namespace std;
int main()
{
	Application app;

	if (!app.Init()) return -1;

	app.Run();

	app.Shutdown();

	return 0;
}
