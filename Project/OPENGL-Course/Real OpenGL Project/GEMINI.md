# GEMINI.md — RAMY Procedural Engine

C++17 / OpenGL 4.3+–4.6 procedural 3D engine, scene editor, and node-based content creation tool (`Real OpenGL Project.sln` / `Real OpenGL Project.vcxproj`).

> Note: Root `../../README.md` mentions GLAD, but the codebase uses **GLEW** (`<GL/glew.h>`, `glew32.lib`, `glew32.dll`).

## Tech Stack & Dependencies

- **Language / Standard**: C++17 (`stdcpp17`), Visual Studio `v145` toolset, Windows (`x64` primary; `Win32` configs exist in `.vcxproj`).
- **Graphics & Windowing**: OpenGL 4.6 Core context (GLEW, GLFW, GLM, `stb_image.h`, `tiny_exr`).
- **Assets**: Assimp (`assimp-vc142-mt.lib`) with `.assbin` binary mesh caching in `Rendering/Model.cpp`.
- **UI**: Dear ImGui + ImNodes (ImGui `#version 460`; **non-docking branch** — `ImGuiConfigFlags_DockingEnable` is disabled, layout uses manual splitters in `EditorUI`), `nlohmann/json`.
- **Physics & Scripting**: Jolt Physics (`joltphysics`), Lua + `sol2`.
- **Testing**: `External Libs/doctest.h`.

## Repository Layout

- Root (`Real OpenGL Project/`): `main.cpp`, `Application.{h,cpp}`, `GraphicsSettings.h`, `graphics_settings.json`, `CommonValues.h`, `Doxyfile`, `imgui.ini`.
- `Core/`: `AssetManager`, `ServiceLocator`, `ResourceHandle`, `Window`, `Camera`, `Frustum`, `Transform`, `InputHandler`, `Grid2D`, `ScriptEngine`, `PerformanceProfiler`.
- `Rendering/`: `Renderer`, `Shader`, `Material`, `Model`, `Mesh`, `MeshData`, `MeshSimplifier`, `PrimitiveGenerator`, `Texture`, `TextureLayer`, `InstancedGroup`, `LODPolicy`, `Impostor`, `CascadedShadowMap`, `OmniShadowMap`, `ShadowMap`, `CloudShadows`, `Wind`, `Atmosphere`, `Skybox`, `GlobalIllumination`, `VolumetricFog`, `AmbientOcclusion` (GTAO), `Reflections` (SSR + probes), `TemporalAA`, `PostProcess`, `WaterFoamMap`.
- `Lighting/`: `Light`, `DirectionalLight`, `PointLight`, `SpotLight`, `LightObject`.
- `Scene/`: `SceneManager`, `SceneSerializer`, `GameObject`, `GameObjectRegistry`, `Component`, `Player`, `Planet`, `RigidBody`, `BoxCollider`, `CapsuleCollider`, `MeshCollider`, `LuaScriptComponent`, `WorldStreamerComponent`, `InfiniteRepeaterComponent`, `UndoManager`, `UndoActions`.
- `Simulation/`: `PhysicsSystem` (Jolt wrapper, `Layers::NON_MOVING=0`, `MOVING=1`), `FluidSimulation` (CPU 2D `Grid2D` shallow-water/erosion solver), `JoltCommon.h`.
- `Nodes/`: `NodeGraph`, `NodeValue`, `NodeRegistry`, `NodeParams`, `NodeAssets`, `NodeRandom`, `GenerationStack`, `GraphMigration`, `OutputNode`, `SceneInputNode`, `SceneObjectsNode`, `InfiniteRepeaterNode`, `CustomNode`, `CustomNodeDef`, `Operation`, `OperationRegistry`, `AllOperations.h`, `Nodes/Math/`, `Nodes/Operations/Op_*.h`.
- `Procedural/`: `PerlinNoiseGenerator`, `PerlinNoiseNode`, `HydraulicErosionNode`, `BeautifulErosionNode`, `DropletErosion.h`, `ThermalErosion.h`, `Noise3D.h`, `RiverNode`, `TerrainLayerMaskNode`, `ScatterNode`, `SpawnInstancesNode`, `CityGridNode`, `BuildingGenNode`, `InteriorGenNode`, `InteriorDecorators`, `InteriorStructure.h`, `RoomOccupancy`, `SolarSystemNode`.
- `Editor/`: `EditorUI`, `DebugOverlay`, `NodeEditorUI`, `NodeBuilderUI`, `ScriptEditor`, `GenerationPipelineUI`, `AssetBrowser`, `TransformGizmo`, `EditorUndo.h`.
- `Assets/` & `Shaders/`: Shaders live in **`Assets/Shaders/`** (primary lookup in `AssetManager::GetShaderPath`, includes `.glsl` headers, PBR, sky/clouds, GI, fog, water, instancing, post-processing) and **`Shaders/`** (fallback lookup for shadows, picking, gizmos, icons, thumbnails, skybox). `Assets/` also holds `Scenes/`, `Materials/`, `Models/`, `Textures/`, `scripts/`, `CustomNodes/`.
- `Tests/`: `CoreTests.cpp`, `EngineTests.cpp`, `LightingTests.cpp`, `NodeGraphTests.cpp`, `NoiseTests.cpp`, `RenderTests.cpp`, `ResourceTests.cpp`, `PerformanceProfilerTests.cpp` (`#ifdef RUN_PERFORMANCE_TESTS`), plus `GISceneTest_tmp.cpp` / `GISettings_tmp.inl` (env-driven `GI_SCENE`/`GI_OUT` visual harness).
- `Tools/`: `build_ultimate_scene_assets.py` & `ultimate_scene_meshes.py` for fetching/preparing local CC0 assets for `Assets/Scenes/Ultimate_Scene`.

## Build & Testing

1. **Jolt Physics Pre-build** (required once before linking): must use dynamic MSVC CRT (`/MD`):
   ```bat
   cd "External Libs\joltphysics\Build"
   cmake_vs2026_cl.bat -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF
   cmake --build VS2026_CL --config Release --target Jolt
   cmake --build VS2026_CL --config Debug --target Jolt
   ```
2. **Project Build**: Build via Visual Studio (`Project/OPENGL-Course/Real OpenGL Project.sln`) or MSBuild (`Debug|x64`, `Release|x64`, `Check Unit Test|x64`).
   - **Adding Source Files**: All `.cpp`/`.h` files compiled by the target must be registered in `Real OpenGL Project.vcxproj` (and `.vcxproj.filters`).
3. **Unit Tests (`doctest`)**:
   - Compiled only under **`Check Unit Test|x64`** (defines `RUN_UNIT_TESTS` and links Jolt `Debug`).
   - `main.cpp` creates a **hidden 64x64 OpenGL 4.6 Core GLFW window** and registers a static `AssetManager` in `ServiceLocator` before running `doctest::Context`. Tests therefore **require an active GPU/OpenGL display context** (will fail headless).
   - Output binary: `x64\Check Unit Test\Real OpenGL Project.exe`.

## Critical Engine & GPU Contracts

### Frame Pipeline (`Application.cpp`)
1. Play Mode physics & Lua updates -> `UpdateImpostors` -> `UpdateWind`.
2. Shadow maps: `CascadedShadowMap` (1–4 cascades) + `OmniShadowMap` (point/spot shadows; `MAX_POINT_LIGHTS = 3`, `MAX_SPOT_LIGHTS = 3` in `CommonValues.h`).
3. `CloudShadowMap::Update` -> `UpdateAtmosphere` -> `GlobalIllumination::Update`.
4. Water `ReflectionPass` (half-res + mipmaps).
5. Viewport HDR FBO (`RGBA16F` + `DEPTH_COMPONENT32F`, `20000.0f` far plane, `60.0f` FOV):
   - Sky pass (half-res raymarch -> `sky_temporal` -> `sky_upsample`)
   - `Renderer::RenderPass` (TAA-jittered projection, GPU culling via `compute_cull.glsl` + indirect draws)
   - `AmbientOcclusion` (GTAO) -> `Reflections` (SSR over probes) -> `Atmosphere::ApplyAerialPerspective` -> `VolumetricFog` (froxels) -> God Rays
6. `TemporalAA::Resolve` (Halton(2,3) 8-sample jitter, YCoCg clamping + Catmull-Rom, motion vectors in HDR attachment 1) -> `PostProcess::Apply` (256x256 GPU log-lum auto-exposure, CAS sharpening, neutral/AgX/ACES tonemapping).
7. Editor icons, picking overlay, ImGui UI (`EditorUI`), and deferred scene/graph/play-mode transitions.

### Shader System (`Rendering/Shader.cpp`)
- **Custom `#include "file"`**: Resolved relative to the current shader directory first, then `Assets/Shaders/`.
- **Auto-Tessellation**: Passing `"river.vert"` to `Shader::CreateFromFiles` auto-attaches `river.tcs` and `river.tes` if present.
- **Dynamic Instancing**: `Renderer::GetInstancedShader()` patches standard shaders with `instanced_header.glsl`, neutralizes `model`/`instanceMatrix`, and dumps `debug_patched_shader.vert`/`.tes` to CWD.
- **NVIDIA Sampler Safety**: Never bind a `sampler2D` to a texture unit declared as `samplerCube` or `sampler2DArray` in the same shader (triggers silent `GL_INVALID_OPERATION` draw drops). `Shader` pre-initializes sampler uniforms on creation.
- **Engine-Managed Uniforms**: Any new engine-controlled uniform MUST be added to `Shader::IsEngineManagedUniform()` so `Material` inspection and `.mat` serialization do not snapshot or overwrite it.
- **Debug Abort**: The OpenGL debug callback in `Application.cpp` aborts on `"glUniform4f"` errors.

### GPU Binding Map (Do Not Collide)
- **UBOs** (`Shader::BindSharedBlocks`):
  - `5`: `WindBlock` (`wind.glsl`)
  - `6`: `CloudShadowBlock` (`180x180` 4-bit nibbles packed into `uvec4[1013]`, `5000.0f` m area)
  - `7`: `FrameBlock` (`frame.glsl`)
  - `8`: `GIBlock` (`gi_block.glsl`, nested toroidal voxel cascades + 2 ambient-cube probe cascades)
- **SSBOs**:
  - `0..9`: GPU compute culling (`compute_cull.glsl`; `1` = `VisibleInstances` / `VisibleInstancesLOD0`)
  - `10`: `GIProbes`
  - `11`: `LayerMasks` (6 bits per layer in fragment shader)
- **Reserved Texture Units**:
  - `0`: `theTexture` (diffuse) | `1`: `normalMap` | `2`: skybox/reflection cubemap | `3`: `directionalShadowMap` (`sampler2DArray`)
  - `4..9`: `omniShadowMaps[0..5].shadowMap` (`samplerCube`)
  - `10, 12, 24, 29, 31`: `textureLayers[0..4]` diffuse (`LAYER_DIFFUSE_UNITS` in `TextureLayer.h`)
  - `11, 13, 28, 30, 32`: `layerNormalMaps[0..4]` (`LAYER_NORMAL_UNITS`)
  - `14..15`: scene depth / reflection
  - `16..19, 33`: `layerDisplacementMaps[0..4]` (`LAYER_DISPLACEMENT_UNITS`)
  - `20`: `directionalShadowColorMap` | `21..23`: point shadow color | `25..27`: spot shadow color
  - `40..41`: 3D cloud noise | `42..44`: Atmosphere transmittance/multiscatter/skyview LUTs

### Geometry, Instancing & Impostors
- **Vertex Layout (`MeshData.h`)**: Interleaved 14 floats: `pos(3) + uv(2) + normal(3) + tangent(3) + bitangent(3)`. `MeshData::TransformBy` multithreads above 1000 vertices; height queries use lazy thread-safe `heightCache`.
- **Instancing & LODs (`InstancedGroup.h`, `LODPolicy.h`)**: 32-byte `PackedInstance`, spatial chunking, shared CPU/GPU LOD policy (`LOD0..2` + `IMPOSTOR = 3`, `CULL_SCREEN_SIZE = 0.002f`, `CROSSFADE_WIDTH = 0.15f`). `rotAndFlags.w > 5.0` encodes instance selection on GPU. `foliageDepthPrepass` renders alpha-cutout depth before `GL_EQUAL` color pass.
- **Impostors**: 8x8 hemi-octahedral baked atlases (`albedo`, `data`, `material`) with coverage-preserving mips.
- **Terrain Layers**: `MAX_TEXTURE_LAYERS = 5`, `LayerBlendMode { Normal, Height, Slope, HeightSlope }`.

## Procedural Node Graph & Editor Conventions

- **Graph Evaluation (`Nodes/`)**:
  - `NodeValue` uses copy-on-write storage (`Float`, `Int`, `Bool`, `Vec2`, `Vec3`, `Mesh`, `Points`, `Objects`) with inline pin parameters.
  - `NodeContext` records `ownedObjects` and `ownedGroups`; keep generated scene entities registered there so re-evaluation cleans up prior outputs.
  - Graph serialization uses `FORMAT_VERSION = 2` with `GraphMigration`.
- **Adding Nodes**:
  - Built-in C++ nodes register in `NodeRegistry::Types()` (`Nodes/NodeRegistry.cpp`).
  - Reusable operations for `CustomNode` (`custom:<name>` definitions in `Assets/CustomNodes/*.json` via `NodeBuilderUI`) use `REGISTER_OPERATION(...)` inside `Nodes/Operations/Op_*.h` and must be included in `Nodes/AllOperations.h`.
- **Assets, Scene, Physics & Lua**:
  - Use `ServiceLocator::GetAssetManager()` for shader/texture/model loading; models load asynchronously (`LoadModelCPU` on worker thread -> `LoadModelGPU` on main thread in `AssetManager::Update()`).
  - Entering Play Mode snapshots scene transforms and auto-attaches static `RigidBody` components to colliders; exiting Play Mode restores transforms.
  - `ScriptEngine` exposes `vec3`, `Transform`, `Component`, `GameObject`, `BodyType`, `RigidBody`, `BoxCollider`, `Resource` / `ResourceHandle` (`Resource.Get`, `Resource.Texture`, `Resource.Model`, `Resource.Audio`, `Resource.NodeGraph`, `Resource.GameObject`, `Resource.FindObject`, `Resource.FromID` with `:is_valid()`, `:resolve()`, `:instantiate()`), and `Input.IsKeyDown` + key constants to Lua; startup runs `Assets/scripts/test_script.lua`.
  - `ScriptEditor` (`Window -> Script Editor`) provides an in-engine multi-tab Lua editor accepting drag-and-drop payloads (`ASSET_PATH`, `MATERIAL_PATH`, `SCRIPT_PATH`, `SCENE_OBJECT_INDEX`) from `AssetBrowser` and Scene Hierarchy.
  - Viewport picking uses **screen points** (not framebuffer pixels) to stay accurate under High-DPI scaling (`InputHandler.cpp`).
  - Settings persist via `Application::{Load,Save}GraphicsSettings` to `graphics_settings.json`.

## Coding Conventions

- Preserve existing comments and docstrings (`Doxyfile` is present at the project root).
- Keep Windows headers guarded against `min`/`max` macro pollution (`#define NOMINMAX` before `<Windows.h>`, as in `Application.cpp`).
- Check existing `Editor/` shortcuts (`W`/`E`/`R` gizmo modes, `X` local/world, `Z` pivot, `Ctrl+Z`/`Ctrl+Shift+Z`/`Ctrl+Y` undo/redo, `Tab`/`Shift+A` node add, `F` frame) before binding new keys.
