-- camera_follow.lua
-- Demonstrates explicit Properties and GameObject selection via Inspector

Properties = {
    target = "GameObject",
    followSpeed = "Float",
    yOffset = "Float",
    zOffset = "Float"
}

target = nil
followSpeed = 5.0
yOffset = 2.0
zOffset = 5.0

function Start()
    log("Camera Follow script started on " .. gameObject:GetName())
end

function Update(dt)
    -- Target is directly a GameObject reference now!
    if target then
        -- Get the target's position
        local targetPos = target.transform:GetPosition()
        
        -- Calculate desired camera position
        local desiredPos = vec3.new(targetPos.x, targetPos.y + yOffset, targetPos.z + zOffset)
        
        -- Interpolate for smooth follow
        local currentPos = gameObject.transform:GetPosition()
        local newX = currentPos.x + (desiredPos.x - currentPos.x) * followSpeed * dt
        local newY = currentPos.y + (desiredPos.y - currentPos.y) * followSpeed * dt
        local newZ = currentPos.z + (desiredPos.z - currentPos.z) * followSpeed * dt
        
        gameObject.transform:SetPosition(vec3.new(newX, newY, newZ))
    end
end
