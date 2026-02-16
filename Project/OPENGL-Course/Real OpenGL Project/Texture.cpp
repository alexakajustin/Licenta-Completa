#include "Texture.h"

Texture::Texture()
{
	textureID = 0;
	width = 0;
	height = 0;
	bitDepth = 0;
	fileLocation = new char[1];
	fileLocation[0] = '\0';
}

Texture::Texture(const char* fileLoc)
{
	textureID = 0;
	width = 0;
	height = 0;
	bitDepth = 0;

	size_t len = strlen(fileLoc) + 1;
	fileLocation = new char[len];
	strcpy_s(fileLocation, len, fileLoc);
}

bool Texture::LoadTexture()
{
	// Force 4 channels (RGBA) for consistency and ease of use in shaders
	unsigned char* texData = stbi_load(fileLocation, &width, &height, &bitDepth, 4);

	printf("Image %s loaded - original channels: %d (forced to 4)\n", fileLocation, bitDepth);

	if (!texData)
	{
		printf("FAILED TO FIND %s!\n", fileLocation);
		return false;
	}

	// We forced 4 channels, so we can always use GL_RGBA
	GLenum format = GL_RGBA;
	GLenum internalFormat = GL_RGBA;
	bitDepth = 4;

	// same thing as the VAO, VBO etc.
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	// linear -> when u zoom in its gonna blend em together
	// nearest -> pixelated look
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// magnify -> going close to the object
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // or GL_NEAREST
	// mipmap -> set of textures dependent on distance
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, texData);
	glGenerateMipmap(GL_TEXTURE_2D);

	// texture is now binded in (video) memory!

	// time to unbind
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(texData);

	return true;
}

// keep LoadTextureA for backwards compatibility - just calls LoadTexture now
bool Texture::LoadTextureA()
{
	return LoadTexture();
}

void Texture::ClearTexture()
{
	glDeleteTextures(1, &textureID);
	textureID = 0;
	width = 0;
	height = 0;
	bitDepth = 0;
	fileLocation = new char[1];
	strcpy_s(fileLocation, 1, "");
}

void Texture::UseTexture()
{
	// when texture is in shader, it needs to reference a sampler which has attached
	// a texture unit
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::UseNormalMap()
{
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, textureID);
}

Texture::~Texture()
{
	ClearTexture();
}
