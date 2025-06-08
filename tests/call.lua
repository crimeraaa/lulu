function f(x, y, z)
    return x, y, z
end

local x, y, z = 9, 10, 11
print(f(x, y, z))

-- Check: do we properly manage variadic returns and calls?
local a = "dummy"
