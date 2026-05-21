# RAMY Procedural Engine

[![Language](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Graphics API](https://img.shields.io/badge/OpenGL-4.3%2B-red.svg)](https://www.opengl.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)](https://microsoft.com)

RAMY is a custom node-based procedural content generation (PCG) engine and real-time renderer built from scratch using C++17 and OpenGL 4.3+. 

I designed it as a visual, interactive editor to make world-building and procedural generation more intuitive. By using a node-graph interface, you can connect different generators, noise functions, and erosion steps to generate massive landscapes, cities, and systems. The engine features a custom GPU-driven rendering pipeline to experiment with modern rendering techniques and culling strategies.

---

## Showcase: San Miguel Benchmark

### Architectural Rendering & Static Batching
![San Miguel Rendering](Licenta/Screenshots/sanmiguel.png)
*Loading and rendering the classic San Miguel benchmark scene. The scene contains over 1,600 individual meshes, which are combined using a custom static batching pipeline to keep the frame rate smooth.*

### Culling Performance Notes
The engine implements multiple compute-shader culling stages, including frustum culling, distance culling, contribution culling, and Hierarchical Z-Buffer (Hi-Z) occlusion culling. 

While Hi-Z occlusion culling is highly effective for large terrain landscapes with mountains blocking the view, it is not a silver bullet. For highly detailed architectural scenes like San Miguel, the overhead of building the depth pyramid/mipmaps and executing the compute pass can exceed the actual rendering cost, making it run slower than standard frustum and distance culling. The editor allows toggling and tuning these settings in real-time to compare their actual performance impact on different scenes.

---

## Key Engine Subsystems

### 1. GPU-Driven Rendering Pipeline
To bypass CPU bottlenecks when rendering massive scenes, the engine processes culling and drawing directly on the GPU:
* **Dynamic Shader Instancifier:** Automatically parses standard material shaders, strips conflicting uniforms (like the model matrix), and patches them with instancing headers at runtime.
* **Compute Shader Culling:** A GPU compute pass that filters out meshes based on frustum intersection, distance/LOD thresholds, and sub-pixel scale. It also supports optional Hi-Z depth-pyramid occlusion checking.
* **Multi-Draw Indirect (MDI):** The compute shader writes draw commands directly into a GPU buffer, enabling the engine to render all active instances using a single `glDrawElementsIndirect` call.
* **Static Batching:** Combines hundreds of independent scene nodes into unified, serialized batches for fast loading and low draw-call counts.

### 2. Lighting & Atmospherics
* **Cascaded Shadow Mapping (CSM):** Directional shadows using dynamic cascade splits. The graphics settings panel lets you adjust the number of active cascades (e.g., comparing 1 vs 2 splits) to see how it resolves shadow aliasing at a distance versus the performance cost.
* **Screen Space Ambient Occlusion (SSAO):** A custom SSAO pass to add depth and contact shadows in complex geometry.
* **Physical Sky & Volumetrics:** Real-time atmospheric scattering simulation coupled with dynamic day/night cycles and volumetric clouds.
* **PBR Shading:** A physically-based rendering pipeline with custom BRDF models.

### 3. Procedural Content Generation (PCG) Nodes
* **Analytical "Beautiful" Erosion:** Wave-based heightmap erosion using PhacelleNoise. It uses Nyquist frequency clamping to prevent aliasing artifacts at grid limits and is parallelized across CPU cores using multi-threading.
* **Hydrological River & Lake Systems:**
  * **Spring Seeding:** Spawns spring points at local height maxima with minimum spacing constraints.
  * **Gradient Descent Pathfinding:** Carves riverbeds downward. If the path hits a depression (sink), it runs a multi-ring search to bridge the gap.
  * **Lake Filling:** Simulates water accumulation in depressions using Dijkstra-based priority queue flood-fill algorithms, generating custom 3D water meshes.
* **Urban & Interior Generation:** Procedural city layouts, building mesh generation, and automated building interiors with room partitioning and furniture decorator rules.

### 4. Editor Tooling & Usability
* **Visual Graph Workspace:** An interactive workspace built with Dear ImGui and ImNodes for building and connecting PCG pipelines.
* **Non-Destructive Action History:** A transaction-based Undo/Redo framework to make layout changes and node tweaking easy to revert.
* **JSON Serialization:** Saves and loads the entire graph, node parameters, and static batch configurations to JSON.

---

## Visual Gallery

### Procedural Landscapes
![Mountain with Rivers](Licenta/Screenshots/5000x5000%20Mountain%20with%20rivers.png)
*A massive 5000x5000 terrain generated via hydraulic erosion and realistic river carving.*

### Solar System Simulation
![Solar System](Licenta/Screenshots/Solar_System.png)
*Procedural planetary systems with configurable orbits, scales, and materials.*

### Procedural Urban Environments
![Generated City](Licenta/Screenshots/Generated_City.png)
*Procedural city grids and structural building blocks generated via the node graph.*

### Engine Interface & Settings
![Landing Window](Licenta/Screenshots/Landing_Window.png)
*The main layout showing the viewport, graph editor, and properties panel.*

![Settings Window](Licenta/Screenshots/Settings_Window.png)
*Granular graphics control panel including performance metrics and shadow cascade settings.*

---

## Technical Stack

* **Core Engine:** C++17
* **Graphics API:** OpenGL 4.3+ (Compute Shaders, SSBOs, MDI)
* **Windowing & Input:** GLFW, GLAD
* **Math Library:** GLM
* **Workspace GUI:** Dear ImGui, ImNodes
* **File Parser & Serializer:** nlohmann/json
* **Asset Loading:** Assimp

## Repository Structure

* `/Project`: Core engine source code (renderer, scene graph, shader compiler, nodes).
* `/Licenta`: Academic thesis documentation, presentations, and screenshots.

---
*Developed as an Academic Bachelor's Thesis.*
