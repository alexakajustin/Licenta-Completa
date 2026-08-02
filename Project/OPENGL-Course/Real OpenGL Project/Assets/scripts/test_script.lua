-- test_script.lua
-- This script runs when the engine initializes.

log("=====================================")
log("  LUA SCRIPTING ENGINE INITIALIZED   ")
log("=====================================")

-- Quick Turing-complete test: Calculate factorial
function factorial(n)
    if n == 0 then
        return 1
    else
        return n * factorial(n - 1)
    end
end

log("Calculating factorial of 5: " .. tostring(factorial(5)))
log("Lua is ready to go!")
