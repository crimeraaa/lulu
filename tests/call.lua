function f(x, y, z)
    return x, y, z
end

function g(x, y, z)
    print(x, y, z)
end

local x, y, z = 9, 10, 11
g(f(x, y, z))

-- Check: do we properly manage variadic returns and calls?
local a = "dummy"
