#include "Rendering/Texture.h"

#include <algorithm>
#include <vector>

// Anisotropic filtering keeps tiled ground/wall textures (and their normal maps)
// sharp at grazing angles instead of smearing into blur. Applies to the bound texture.
static void ApplyAnisotropicFiltering()
{
	if (!GLEW_EXT_texture_filter_anisotropic) return;
	GLfloat maxAniso = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso < 16.0f ? maxAniso : 16.0f);
}

// Alpha-tested textures (foliage, grass and fern cards) lose coverage in the smaller mips:
// averaging thin leaves with the transparent background drops alpha under the 0.5 cutoff, so
// trees go bald and grass thins out with distance. The mip chain of such textures is built on
// the CPU instead, and each level's alpha is rescaled so the fraction of texels passing the
// alpha test matches level 0 (Castano, "Computing Alpha Mipmaps", 2010).
static float AlphaCoverage(const std::vector<float>& rgba, float scale)
{
	size_t passed = 0, count = rgba.size() / 4;
	for (size_t i = 0; i < count; i++)
		if (rgba[i * 4 + 3] * scale >= 0.5f) passed++;
	return count ? (float)passed / (float)count : 0.0f;
}

static void UploadCoveragePreservingMips(const unsigned char* data, int width, int height)
{
	std::vector<float> level((size_t)width * height * 4);
	for (size_t i = 0; i < level.size(); i++) level[i] = data[i] / 255.0f;
	const float targetCoverage = AlphaCoverage(level, 1.0f);

	std::vector<unsigned char> upload;
	int w = width, h = height;
	for (int mip = 1; w > 1 || h > 1; mip++)
	{
		int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
		std::vector<float> next((size_t)nw * nh * 4);
		for (int y = 0; y < nh; y++)
		for (int x = 0; x < nw; x++)
		{
			float rgb[3] = { 0, 0, 0 }, rgbPlain[3] = { 0, 0, 0 }, alphaSum = 0.0f;
			for (int k = 0; k < 4; k++)
			{
				int sx = std::min(x * 2 + (k & 1), w - 1), sy = std::min(y * 2 + (k >> 1), h - 1);
				const float* t = &level[((size_t)sy * w + sx) * 4];
				for (int c = 0; c < 3; c++) { rgb[c] += t[c] * t[3]; rgbPlain[c] += t[c]; }
				alphaSum += t[3];
			}
			float* o = &next[((size_t)y * nw + x) * 4];
			// Alpha-weighted colour: the (invisible) background colour does not bleed into the leaves
			for (int c = 0; c < 3; c++) o[c] = alphaSum > 1e-4f ? rgb[c] / alphaSum : rgbPlain[c] * 0.25f;
			o[3] = alphaSum * 0.25f;
		}

		// Smallest alpha scale that restores the level-0 coverage (coverage grows with scale)
		float lo = 0.0f, hi = 8.0f;
		for (int it = 0; it < 14; it++) {
			float mid = 0.5f * (lo + hi);
			if (AlphaCoverage(next, mid) < targetCoverage) lo = mid; else hi = mid;
		}
		upload.resize(next.size());
		for (size_t i = 0; i < next.size(); i++) {
			float v = (i % 4 == 3) ? std::min(next[i] * hi, 1.0f) : next[i];
			upload[i] = (unsigned char)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
		glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, upload.data());

		level.swap(next); // Next level is filtered from the unscaled one, so scaling never compounds
		w = nw; h = nh;
	}
}

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
	: textureID(other.textureID), width(other.width), height(other.height), bitDepth(other.bitDepth), fileLocation(other.fileLocation), rawData(other.rawData),
	  hasCutoutAlpha(other.hasCutoutAlpha)
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
		hasCutoutAlpha = other.hasCutoutAlpha;

		other.textureID = 0;
		other.fileLocation = nullptr;
		other.rawData = nullptr;
	}
	return *this;
}

bool Texture::LoadTextureCPU()
{
	if (rawData) return true; // Already loaded (e.g., embedded GLB texture)
	rawData = stbi_load(fileLocation, &width, &height, &bitDepth, 4);
	if (!rawData) {
		printf("FAILED TO FIND %s!\n", fileLocation);
		return false;
	}
	// Only sources with an alpha channel can cut out (stbi pads RGB to alpha 255)
	if (bitDepth == 2 || bitDepth == 4) DetectCutoutAlpha();
	bitDepth = 4;
	return true;
}

void Texture::DetectCutoutAlpha()
{
	// Same threshold as the alpha test in the shaders (discard below 0.5)
	hasCutoutAlpha = false;
	if (!rawData) return;
	size_t pixelCount = (size_t)width * (size_t)height;
	for (size_t p = 0; p < pixelCount; p++) {
		if (rawData[p * 4 + 3] < 128) { hasCutoutAlpha = true; return; }
	}
}

bool Texture::LoadFromMemory(const unsigned char* compressedData, int dataSize)
{
	if (rawData) return true;
	rawData = stbi_load_from_memory(compressedData, dataSize, &width, &height, &bitDepth, 4);
	if (!rawData) {
		printf("[Texture] Failed to decode embedded texture (%d bytes)\n", dataSize);
		return false;
	}
	if (bitDepth == 2 || bitDepth == 4) DetectCutoutAlpha();
	bitDepth = 4;
	printf("[Texture] Loaded embedded texture: %dx%d from %d bytes\n", width, height, dataSize);
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
	ApplyAnisotropicFiltering();

	// Use sized internal format GL_RGBA8 for better driver compatibility
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
	
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("[Texture] GL Error during glTexImage2D: 0x%X (ID: %d, Size: %dx%d)\n", err, textureID, width, height);
		return false;
	}

	if (hasCutoutAlpha && bitDepth == 4) UploadCoveragePreservingMips(texData, width, height);
	else glGenerateMipmap(GL_TEXTURE_2D);

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
	ApplyAnisotropicFiltering();

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
