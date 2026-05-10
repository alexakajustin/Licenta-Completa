import re
import os

scene_cpp_path = r"c:\Users\Justin\Desktop\Licenta-Completa\Project\OPENGL-Course\Real OpenGL Project\SceneManager.cpp"
scene_h_path = r"c:\Users\Justin\Desktop\Licenta-Completa\Project\OPENGL-Course\Real OpenGL Project\SceneManager.h"
renderer_cpp_path = r"c:\Users\Justin\Desktop\Licenta-Completa\Project\OPENGL-Course\Real OpenGL Project\Renderer.cpp"

with open(scene_cpp_path, 'r', encoding='utf-8') as f:
    scene_cpp = f.read()

# 1. Extract RenderAll and GenerateHiZMap
def extract_function(source, func_name):
    # Find the start of the function
    pattern = r"void\s+SceneManager::" + func_name + r"\s*\("
    match = re.search(pattern, source)
    if not match:
        return None, source
    
    start_idx = match.start()
    
    # Find the matching closing brace
    brace_count = 0
    in_func = False
    end_idx = start_idx
    for i in range(start_idx, len(source)):
        if source[i] == '{':
            brace_count += 1
            in_func = True
        elif source[i] == '}':
            brace_count -= 1
            if in_func and brace_count == 0:
                end_idx = i + 1
                break
    
    func_code = source[start_idx:end_idx]
    new_source = source[:start_idx] + source[end_idx:]
    return func_code, new_source

renderall_code, scene_cpp = extract_function(scene_cpp, "RenderAll")
generatehiz_code, scene_cpp = extract_function(scene_cpp, "GenerateHiZMap")

# 2. Modify RenderAll to Renderer::RenderScene
renderall_code = renderall_code.replace("void SceneManager::RenderAll", "void Renderer::RenderScene")
renderall_code = re.sub(r"void Renderer::RenderScene\s*\((.*?)\)", r"void Renderer::RenderScene(SceneManager* scene, \1)", renderall_code, 1)

# Replace internal SceneManager variable usages
replacements = {
    r"\bobjects\b": "scene->GetObjects()",
    r"\bselectedObjectIndices\b": "scene->GetSelectedObjectIndices()",
    r"\binstancedGroups\b": "scene->GetInstancedGroups()",
    r"\bgraphicsSettings\b": "gs",
    r"\bmainShader\b": "scene->GetMainShader()",
    r"\bcullShader\b": "instancedCullShader", # Wait, instancedCullShader is Renderer's, but SceneManager uses cullShader
    r"\binstancedRenderShader\b": "this->instancedRenderShader", # Actually, renderer has its own
    r"\brenderDistanceMultiplier\b": "scene->GetRenderDistanceMultiplier()",
    r"\bshadowDistanceMultiplier\b": "scene->GetShadowDistanceMultiplier()",
    r"\bpickingShader\b": "scene->pickingShader", # Wait, pickingShader is private in SceneManager. Let's not access it directly if we don't have to.
    r"\bpickingInitialized\b": "scene->pickingInitialized"
}

# We need to manually fix cullShader usage in RenderAll
renderall_code = renderall_code.replace("scene->cullShader", "instancedCullShader")
renderall_code = renderall_code.replace("cullShader", "instancedCullShader")
renderall_code = renderall_code.replace("instancedRenderShader", "this->instancedRenderShader")

for k, v in replacements.items():
    if k not in [r"\bcullShader\b", r"\binstancedRenderShader\b"]:
        renderall_code = re.sub(k, v, renderall_code)

# Fix double scene->scene-> issue if it happens
renderall_code = renderall_code.replace("scene->scene->", "scene->")

# Rename GenerateHiZMap
generatehiz_code = generatehiz_code.replace("void SceneManager::GenerateHiZMap", "void Renderer::GenerateHiZMap")

# 3. Save modified Renderer.cpp
with open(renderer_cpp_path, 'r', encoding='utf-8') as f:
    renderer_cpp = f.read()

renderer_cpp += "\n" + renderall_code + "\n\n" + generatehiz_code + "\n"

# Replace scene.RenderAll calls in Renderer.cpp
renderer_cpp = re.sub(r"scene\.RenderAll\((.*?)\)", r"this->RenderScene(&scene, \1)", renderer_cpp)

with open(renderer_cpp_path, 'w', encoding='utf-8') as f:
    f.write(renderer_cpp)

# 4. Clean up SceneManager.h
with open(scene_h_path, 'r', encoding='utf-8') as f:
    scene_h = f.read()

scene_h = re.sub(r"void RenderAll\(.*?\);", "", scene_h, flags=re.DOTALL)
scene_h = re.sub(r"// Hi-Z Occlusion Culling.*?glm::mat4 prevViewProj = glm::mat4\(1\.0f\);", "", scene_h, flags=re.DOTALL)
scene_h = re.sub(r"void GenerateHiZMap\(.*?\);", "", scene_h)
scene_h = re.sub(r"GLuint GetHiZTexture\(\).*?void GenerateHiZDebug\(float nearPlane, float farPlane\);", "", scene_h, flags=re.DOTALL)

with open(scene_h_path, 'w', encoding='utf-8') as f:
    f.write(scene_h)

# 5. Clean up SceneManager.cpp
scene_cpp = re.sub(r"void SceneManager::GenerateHiZDebug.*?\}", "", scene_cpp, flags=re.DOTALL) # remove GenerateHiZDebug
with open(scene_cpp_path, 'w', encoding='utf-8') as f:
    f.write(scene_cpp)

print("Refactoring complete.")
