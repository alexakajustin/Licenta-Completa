#include "Texture.h"

Texture::Texture()
	: textureID(0), width(0), height(0), bitDepth(0), fileLocation(nullptr)
{
}

Texture::Texture(const char* fileLoc)
	: textureID(0), width(0), height(0), bitDepth(0), rawData(nullptr)
{
	size_t len = strlen(fileLoc) + 1;
	fileLocation = new char[len];
	strcpy_s(fileLocation, len, fileLoc);
}

// Move constructor
Texture::Texture(Texture&& other) noexcept
	: textureID(other.textureID), width(other.width), height(other.height), bitDepth(other.bitDepth), fileLocation(other.fileLocation), rawData(other.rawData)
{
	other.textureID = 0; // Prevent deletion in other's destructor
	other.fileLocation = nullptr;
	other.rawData = nullptr;
}

// Move assignment
Texture& Texture::operator=(Texture&& other) noexcept
{
	if (this != &other) {
		ClearTexture();
		
		textureID = other.textureID;
		width = other.width;
		height = other.height;
		bitDepth = other.bitDepth;
		fileLocation = other.fileLocation;
		rawData = other.rawData;
		
		other.textureID = 0;
		other.fileLocation = nullptr;
		other.rawData = nullptr;
	}
	return *this;
}

bool Texture::LoadTextureCPU()
{
	rawData = stbi_load(fileLocation, &width, &height, &bitDepth, 4);
	if (!rawData) {
		printf("FAILED TO FIND %s!\n", fileLocation);
		return false;
	}
	bitDepth = 4;
	return true;
}

bool Texture::LoadTextureGPU()
{
	if (!rawData) return false;
	bool result = LoadTextureFromData(rawData, width, height, bitDepth);
	
	// Caller now owns the memory lifecycle
	stbi_image_free(rawData);
	rawData = nullptr;
	
	return result;
}

bool Texture::LoadTexture()
{
	if (!LoadTextureCPU()) return false;
	return LoadTextureGPU();
}

bool Texture::LoadTextureFromData(unsigned char* texData, int w, int h, int bitD)
{
	if (!texData || w <= 0 || h <= 0) {
		return false;
	}

	// Safety: Clear existing GPU texture before creating a new one
	ClearTexture();

	width = w;
	height = h;
	bitDepth = bitD;

	glGenTextures(1, &textureID);
	if (textureID == 0) return false;

	glBindTexture(GL_TEXTURE_2D, textureID);

	// Fortress: Reset ALL pixel storage states to defaults
	// These might have been left in a dirty state by other middleware (ImGui, etc.)
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 

	// Use sized internal format GL_RGBA8 for better driver compatibility
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
	
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("[Texture] GL Error during glTexImage2D: 0x%X (ID: %d, Size: %dx%d)\n", err, textureID, width, height);
		return false;
	}

	glGenerateMipmap(GL_TEXTURE_2D);

	// Restore alignment to default (4) for other engine operations
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	return true;
}

// keep LoadTextureA for backwards compatibility - just calls LoadTexture now
bool Texture::LoadTextureA()
{
	return LoadTexture();
}

void Texture::ClearTexture()
{
	if (textureID != 0) {
		glDeleteTextures(1, &textureID);
		textureID = 0;
	}
}

bool Texture::LoadTextureGrayscale()
{
	// Load as single channel for displacement/height maps (4x less memory than RGBA)
	unsigned char* texData = stbi_load(fileLocation, &width, &height, &bitDepth, 1);

	if (!texData)
	{
		printf("FAILED TO FIND %s!\n", fileLocation);
		return false;
	}

	printf("Displacement map %s loaded - %dx%d (grayscale, original channels: %d)\n", fileLocation, width, height, bitDepth);

	// Auto-downscale oversized textures to prevent GPU memory exhaustion
	bool wasDownscaled = false;
	while (width > 4096 || height > 4096)
	{
		int newW = width / 2;
		int newH = height / 2;
		if (newW < 1) newW = 1;
		if (newH < 1) newH = 1;
		unsigned char* downscaled = (unsigned char*)malloc(newW * newH);
		if (!downscaled) {
			if (wasDownscaled) free(texData); else stbi_image_free(texData);
			return false;
		}
		for (int y = 0; y < newH; y++)
			for (int x = 0; x < newW; x++)
				downscaled[y * newW + x] = texData[(y * 2) * width + (x * 2)];
		if (wasDownscaled) free(texData); else stbi_image_free(texData);
		texData = downscaled;
		wasDownscaled = true;
		width = newW;
		height = newH;
		printf("  -> Downscaled to %dx%d\n", width, height);
	}

	bitDepth = 1;

	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	// Fortress: Reset ALL pixel storage states
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, texData);
	
	// Swizzle mask: Map the Single Red channel to R, G, and B components 
	// This makes the texture appear grayscale instead of red in shaders and UI
	GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

	glGenerateMipmap(GL_TEXTURE_2D);

	// Restore alignment to default
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glBindTexture(GL_TEXTURE_2D, 0);

	if (wasDownscaled) free(texData); else stbi_image_free(texData);

	return true;
}

void Texture::UseTexture()
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::UseNormalMap()
{
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::UseTextureOnUnit(GLenum unit)
{
	glActiveTexture(unit);
	glBindTexture(GL_TEXTURE_2D, textureID);
}

Texture::~Texture()
{
	ClearTexture();
	if (fileLocation) {
		delete[] fileLocation;
		fileLocation = nullptr;
	}
	if (rawData) {
		stbi_image_free(rawData);
		rawData = nullptr;
	}
}
