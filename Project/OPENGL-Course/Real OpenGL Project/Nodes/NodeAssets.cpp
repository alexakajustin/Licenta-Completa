#include "Nodes/NodeAssets.h"
#include "Rendering/Texture.h"
#include <map>

Texture* NodeAssets::GetTexture(const std::string& path)
{
	// Kept until the process ends on purpose: deleting them at static destruction would call
	// OpenGL after the context is gone
	static std::map<std::string, Texture*> cache;
	auto it = cache.find(path);
	if (it != cache.end()) return it->second;
	Texture* tex = new Texture(path.c_str());
	tex->LoadTexture();
	cache[path] = tex;
	return tex;
}
