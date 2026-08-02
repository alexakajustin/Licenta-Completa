-- rotate_test.lua
-- Attached via LuaScriptComponent to a GameObject

local speed = 45.0 -- degrees per second

function Start()
    log("Started Lua Script on GameObject: " .. gameObject:GetName())
end

function Update(dt)
    -- Get the current transform
    local t = gameObject.transform
    local currentRot = t:GetRotation()
    
    -- Increment the Y rotation
    currentRot.y = currentRot.y + (speed * dt)
    
    -- Apply it back
    t:SetRotation(currentRot)
end
