#ifdef RUN_UNIT_TESTS

#include "External Libs/doctest.h"
#include "Rendering/Shader.h"
#include <GL/glew.h>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>


// =====================================================================
// Helper: compile a vert+frag shader pair and check it linked
// =====================================================================
static void CheckVertFrag(const char* vert, const char* frag)
{
    Shader s;
    s.CreateFromFiles(vert, frag);
    INFO("Shader pair: " << vert << " + " << frag);
    CHECK(s.GetShaderID() != 0);
}

// =====================================================================
// Helper: compile a vert+geom+frag shader triple and check it linked
// =====================================================================
static void CheckVertGeomFrag(const char* vert, const char* geom, const char* frag)
{
    Shader s;
    s.CreateFromFiles(vert, geom, frag);
    INFO("Shader triple: " << vert << " + " << geom << " + " << frag);
    CHECK(s.GetShaderID() != 0);
}

// =====================================================================
// Helper: compile a tessellation shader (vert+tcs+tes+frag)
// =====================================================================
static void CheckTess(const char* vert, const char* tcs, const char* tes, const char* frag)
{
    Shader s;
    s.CreateFromFiles(vert, tcs, tes, frag);
    INFO("Tess shader: " << vert << " + " << tcs << " + " << tes << " + " << frag);
    CHECK(s.GetShaderID() != 0);
}

// =====================================================================
// Helper: compile a compute shader
// =====================================================================
static void CheckCompute(const char* path)
{
    Shader s;
    s.CreateComputeShader(path);
    INFO("Compute shader: " << path);
    CHECK(s.GetShaderID() != 0);
}

// =====================================================================
TEST_CASE("Shader Compilation - Core Rendering Shaders")
{
    SUBCASE("Main forward shader")
    {
        CheckVertFrag("Assets/Shaders/shader.vert", "Assets/Shaders/shader.frag");
    }

    SUBCASE("Directional shadow map")
    {
        CheckVertFrag("Shaders/directional_shadow_map.vert", "Shaders/directional_shadow_map.frag");
    }

    SUBCASE("Omni shadow map (vert+geom+frag)")
    {
        CheckVertGeomFrag("Shaders/omni_shadow_map.vert", "Shaders/omni_shadow_map.geom", "Shaders/omni_shadow_map.frag");
    }

    SUBCASE("Instanced render shader")
    {
        CheckVertFrag("Assets/Shaders/instanced_object.vert", "Assets/Shaders/shader.frag");
    }

    SUBCASE("Instanced shadow shader")
    {
        CheckVertFrag("Shaders/instanced_shadow.vert", "Shaders/instanced_shadow.frag");
    }

    SUBCASE("Instanced omni shadow (vert+geom+frag)")
    {
        CheckVertGeomFrag("Shaders/instanced_omni_shadow.vert", "Shaders/omni_shadow_map.geom", "Shaders/omni_shadow_map.frag");
    }
}

// =====================================================================
TEST_CASE("Shader Compilation - Tessellation Shaders")
{
    SUBCASE("Terrain tessellation")
    {
        CheckTess(
            "Assets/Shaders/shader_tess.vert",
            "Assets/Shaders/terrain_tess.tcs",
            "Assets/Shaders/terrain_tess.tes",
            "Assets/Shaders/shader.frag");
    }

    SUBCASE("Directional shadow tessellation")
    {
        CheckTess(
            "Assets/Shaders/shader_tess.vert",
            "Assets/Shaders/directional_shadow_map_tess.tcs",
            "Assets/Shaders/directional_shadow_map_tess.tes",
            "Shaders/directional_shadow_map.frag");
    }

    SUBCASE("Planet tessellation")
    {
        CheckTess(
            "Assets/Shaders/planet.vert",
            "Assets/Shaders/planet_tess.tcs",
            "Assets/Shaders/planet_tess.tes",
            "Assets/Shaders/planet.frag");
    }

    SUBCASE("Sun tessellation")
    {
        CheckTess(
            "Assets/Shaders/sun.vert",
            "Assets/Shaders/sun_tess.tcs",
            "Assets/Shaders/sun_tess.tes",
            "Assets/Shaders/sun.frag");
    }
}

// =====================================================================
TEST_CASE("Shader Compilation - Compute Shaders")
{
    SUBCASE("GPU frustum cull compute")
    {
        CheckCompute("Assets/Shaders/compute_cull.glsl");
    }

    SUBCASE("Hi-Z downsample compute")
    {
        CheckCompute("Assets/Shaders/hiz_downsample.glsl");
    }

    SUBCASE("Hi-Z copy compute")
    {
        CheckCompute("Assets/Shaders/hiz_copy.glsl");
    }

    SUBCASE("Hi-Z debug compute")
    {
        CheckCompute("Assets/Shaders/hiz_debug.glsl");
    }

    SUBCASE("Object cull compute")
    {
        CheckCompute("Assets/Shaders/object_cull.glsl");
    }
}

// =====================================================================
TEST_CASE("Shader Compilation - Post-Processing & Sky Shaders")
{
    SUBCASE("SSAO shader")
    {
        CheckVertFrag("Assets/Shaders/ssao.vert", "Assets/Shaders/ssao.frag");
    }

    SUBCASE("SSAO blur shader")
    {
        CheckVertFrag("Assets/Shaders/ssao.vert", "Assets/Shaders/ssao_blur.frag");
    }

    SUBCASE("SSAO apply shader")
    {
        CheckVertFrag("Assets/Shaders/ssao.vert", "Assets/Shaders/ssao_apply.frag");
    }

    SUBCASE("Godray shader")
    {
        CheckVertFrag("Assets/Shaders/godrays.vert", "Assets/Shaders/godrays.frag");
    }

    SUBCASE("Volumetric sky shader")
    {
        CheckVertFrag("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/volumetric_sky.frag");
    }

    SUBCASE("Universe sky shader")
    {
        CheckVertFrag("Assets/Shaders/volumetric_sky.vert", "Assets/Shaders/universe_sky.frag");
    }
}

// =====================================================================
TEST_CASE("Shader Compilation - Editor & Utility Shaders")
{
    SUBCASE("Picking shader")
    {
        CheckVertFrag("Shaders/picking.vert", "Shaders/picking.frag");
    }

    SUBCASE("Icon shader")
    {
        CheckVertFrag("Shaders/icon.vert", "Shaders/icon.frag");
    }

    SUBCASE("Gizmo shader")
    {
        CheckVertFrag("Shaders/gizmo.vert", "Shaders/gizmo.frag");
    }

    SUBCASE("Material preview shader")
    {
        CheckVertFrag("Shaders/materialPreview.vert", "Shaders/materialPreview.frag");
    }

    SUBCASE("Thumbnail shader")
    {
        CheckVertFrag("Shaders/thumbnail.vert", "Shaders/thumbnail.frag");
    }

    SUBCASE("Skybox shader")
    {
        CheckVertFrag("Shaders/skybox.vert", "Shaders/skybox.frag");
    }
}

// =====================================================================
TEST_CASE("Shader Compilation - World Shaders")
{
    SUBCASE("Grass shader")
    {
        CheckVertFrag("Assets/Shaders/grass.vert", "Assets/Shaders/grass.frag");
    }

    SUBCASE("Water shader")
    {
        CheckVertFrag("Assets/Shaders/water.vert", "Assets/Shaders/water.frag");
    }

    SUBCASE("River shader")
    {
        CheckVertFrag("Assets/Shaders/river.vert", "Assets/Shaders/river.frag");
    }

    SUBCASE("Planet shader (no tess)")
    {
        CheckVertFrag("Assets/Shaders/planet.vert", "Assets/Shaders/planet.frag");
    }

    SUBCASE("Sun shader (no tess)")
    {
        CheckVertFrag("Assets/Shaders/sun.vert", "Assets/Shaders/sun.frag");
    }
}

// =====================================================================
TEST_CASE("Framebuffer Completeness - Basic FBO")
{
    SUBCASE("Color + Depth FBO (viewport-like)")
    {
        GLuint fbo, colorTex, depthTex;
        int w = 256, h = 256;

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // Color attachment
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

        // Depth attachment
        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        CHECK(status == GL_FRAMEBUFFER_COMPLETE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteTextures(1, &depthTex);
    }

    SUBCASE("Depth-Only FBO (shadow map-like)")
    {
        GLuint fbo, depthTex;
        int size = 1024;

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);

        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        CHECK(status == GL_FRAMEBUFFER_COMPLETE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &depthTex);
    }

    SUBCASE("Reflection FBO (color + depth)")
    {
        GLuint fbo, colorTex, depthTex;
        int w = 960, h = 540;

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        CHECK(status == GL_FRAMEBUFFER_COMPLETE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteTextures(1, &depthTex);
    }
}

#endif // RUN_UNIT_TESTS
