-- physics_test.lua
-- Demonstrates dynamic component creation and physics manipulation

local rb = nil
local speed = 100.0
local jumpForce = 20.0

function Start()
    log("Physics test script started on " .. gameObject:GetName())
    
    -- Check if we already have a RigidBody
    rb = gameObject:GetRigidBody()
    
    if not rb then
        log("No RigidBody found. Adding one dynamically...")
        
        -- Add a BoxCollider so we can actually collide with things
        local collider = gameObject:AddBoxCollider()
        collider.size = vec3.new(1.0, 1.0, 1.0)
        
        -- Add and configure the RigidBody
        rb = gameObject:AddRigidBody()
        rb:SetType(BodyType.Dynamic)
        rb:SetMass(1.0)
        rb:SetFriction(0.5)
        
        log("RigidBody and BoxCollider added successfully!")
    else
        log("Existing RigidBody found.")
    end
end

function Update(dt)
    if not rb then return end
    
    -- We can apply forces or impulses using the Input system!
    
    if Input.IsKeyDown(Input.KEY_SPACE) then
        rb:AddImpulse(vec3.new(0.0, jumpForce * dt, 0.0))
    end
    
    if Input.IsKeyDown(Input.KEY_W) then
        rb:AddForce(vec3.new(0.0, 0.0, -speed))
    end
    
    if Input.IsKeyDown(Input.KEY_S) then
        rb:AddForce(vec3.new(0.0, 0.0, speed))
    end
    
    if Input.IsKeyDown(Input.KEY_A) then
        rb:AddForce(vec3.new(-speed, 0.0, 0.0))
    end
    
    if Input.IsKeyDown(Input.KEY_D) then
        rb:AddForce(vec3.new(speed, 0.0, 0.0))
    end
end
