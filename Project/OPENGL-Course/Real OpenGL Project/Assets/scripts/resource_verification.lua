-- Assets/scripts/resource_verification.lua
-- Play Mode verification script for the Unified Resource Architecture ("Everything is a Resource")
-- Exercises resource handles, safe fallbacks, and runtime lifecycle across frames.

local textureHandle = nil
local modelHandle = nil
local audioHandle = nil
local nodeGraphHandle = nil
local targetObjectHandle = nil
local badHandle = nil
local badObjectHandle = nil

local frameCount = 0
local totalChecksPassed = 0

function Start()
    log("[ResourceVerification] === Starting Unified Resource Verification ===")

    if Resource == nil then
        log("[ResourceVerification] WARNING: Global 'Resource' table not available in current environment.")
        return
    end

    -- 1. Texture Resource Handle
    textureHandle = Resource.Get("Assets/Textures/plain.png")
    if textureHandle ~= nil then
        log("[ResourceVerification] Texture handle path: " .. tostring(textureHandle.path))
        log("[ResourceVerification] Texture handle type: " .. tostring(textureHandle:type_name()))
        log("[ResourceVerification] Texture handle valid: " .. tostring(textureHandle:is_valid()))
        totalChecksPassed = totalChecksPassed + 1
        
        -- Resolve texture
        local texObj = textureHandle:resolve()
        if texObj ~= nil then
            log("[ResourceVerification] Texture successfully resolved to engine object")
        else
            log("[ResourceVerification] Texture resolve returned nil (file not on disk or uncompiled)")
        end
    end

    -- Also verify typed helper Resource.Texture
    if Resource.Texture ~= nil then
        local typedTex = Resource.Texture("Assets/Textures/plain.png")
        if typedTex ~= nil and typedTex:type_name() == "Texture" then
            log("[ResourceVerification] Resource.Texture() helper verified")
            totalChecksPassed = totalChecksPassed + 1
        end
    end

    -- 2. Model Resource Handle
    modelHandle = Resource.Get("Assets/Models/grass.obj")
    if modelHandle ~= nil then
        log("[ResourceVerification] Model handle path: " .. tostring(modelHandle.path))
        log("[ResourceVerification] Model handle type: " .. tostring(modelHandle:type_name()))
        totalChecksPassed = totalChecksPassed + 1
    end

    if Resource.Model ~= nil then
        local typedModel = Resource.Model("Assets/Models/grass.obj")
        if typedModel ~= nil and typedModel:type_name() == "Model" then
            log("[ResourceVerification] Resource.Model() helper verified")
            totalChecksPassed = totalChecksPassed + 1
        end
    end

    -- 3. Audio Resource Handle
    audioHandle = Resource.Get("Assets/Audio/ambient.wav")
    if audioHandle ~= nil then
        log("[ResourceVerification] Audio handle path: " .. tostring(audioHandle.path))
        log("[ResourceVerification] Audio handle type: " .. tostring(audioHandle:type_name()))
        totalChecksPassed = totalChecksPassed + 1
    end

    if Resource.Audio ~= nil then
        local typedAudio = Resource.Audio("Assets/Audio/test.ogg")
        if typedAudio ~= nil and typedAudio:type_name() == "Audio" then
            log("[ResourceVerification] Resource.Audio() helper verified")
            totalChecksPassed = totalChecksPassed + 1
        end
    end

    -- 4. NodeGraph Resource Handle
    nodeGraphHandle = Resource.Get("Assets/Scenes/City/City_Grid.json")
    if nodeGraphHandle ~= nil then
        log("[ResourceVerification] NodeGraph handle identifier: " .. tostring(nodeGraphHandle.identifier))
        log("[ResourceVerification] NodeGraph handle type: " .. tostring(nodeGraphHandle:type_name()))
        totalChecksPassed = totalChecksPassed + 1
    end

    if Resource.NodeGraph ~= nil then
        local typedGraph = Resource.NodeGraph("generator")
        if typedGraph ~= nil and typedGraph:type_name() == "NodeGraph" then
            log("[ResourceVerification] Resource.NodeGraph() helper verified")
            totalChecksPassed = totalChecksPassed + 1
        end
    end

    -- 5. GameObject Resource Handle
    local selfName = "LuaTestBox"
    if gameObject ~= nil and gameObject.GetName ~= nil then
        selfName = gameObject:GetName()
    end

    targetObjectHandle = Resource.FindObject(selfName)
    if targetObjectHandle ~= nil and targetObjectHandle:is_valid() then
        log("[ResourceVerification] Found self GameObject handle: " .. tostring(targetObjectHandle.identifier))
        log("[ResourceVerification] GameObject handle type: " .. tostring(targetObjectHandle:type_name()))
        local resolvedObj = targetObjectHandle:resolve()
        if resolvedObj ~= nil then
            log("[ResourceVerification] Successfully resolved GameObject: " .. resolvedObj:GetName())
            totalChecksPassed = totalChecksPassed + 1
        end
    else
        log("[ResourceVerification] GameObject handle query completed (safe fallback)")
    end

    -- 6. Safe Error Handling on Non-Existent Resources
    badHandle = Resource.Get("Assets/Textures/definitely_non_existent_404.png")
    if badHandle ~= nil then
        local isBadValid = badHandle:is_valid()
        assert(isBadValid == false, "Non-existent resource must report is_valid() == false")
        local badResolved = badHandle:resolve()
        assert(badResolved == nil, "Resolving invalid handle must safely return nil")
        log("[ResourceVerification] Safe error handling for missing asset verified (returned nil)")
        totalChecksPassed = totalChecksPassed + 1
    end

    badObjectHandle = Resource.FindObject("NonExistent_Ghost_GameObject_#9999")
    if badObjectHandle ~= nil then
        local isBadObjValid = badObjectHandle:is_valid()
        assert(isBadObjValid == false, "Non-existent GameObject must report is_valid() == false")
        local badObjResolved = badObjectHandle:resolve()
        assert(badObjResolved == nil, "Resolving non-existent GameObject must return nil")
        log("[ResourceVerification] Safe error handling for missing GameObject verified (returned nil)")
        totalChecksPassed = totalChecksPassed + 1
    end

    log("[ResourceVerification] === Start Phase Completed. Checks passed: " .. tostring(totalChecksPassed) .. " ===")
end

function Update(dt)
    frameCount = frameCount + 1

    -- Periodic runtime lifecycle checks
    if frameCount % 60 == 0 then
        -- 1. Check target object handle resilience across frames
        if targetObjectHandle ~= nil and targetObjectHandle:is_valid() then
            local obj = targetObjectHandle:resolve()
            if obj ~= nil then
                local t = obj.transform
                if t ~= nil and t.GetPosition ~= nil then
                    local p = t:GetPosition()
                end
            end
        end

        -- 2. Ensure calling resolve on invalid handles remains idempotent and safe
        if badHandle ~= nil then
            local res = badHandle:resolve()
            assert(res == nil, "Repeated resolve on invalid handle must remain nil")
        end

        if badObjectHandle ~= nil then
            local res = badObjectHandle:resolve()
            assert(res == nil, "Repeated resolve on invalid GameObject handle must remain nil")
        end
    end
end
