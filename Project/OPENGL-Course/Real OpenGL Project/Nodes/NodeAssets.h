#pragma once

#include <string>

class Texture;

// Textures generated content uses (roads, walls, floors...): loaded once and shared for the
// session. Every run of a node used to load its textures again, and never freed them.
namespace NodeAssets
{
	Texture* GetTexture(const std::string& path);
}
