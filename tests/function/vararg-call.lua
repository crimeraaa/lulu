---@param x integer
---@param y integer
---@param z integer
local function f(x, y, z)
    return x, y, z
end

local function g()
    return 9, 10, 21, 42
end

print(f(g()))

-- return f(g())
-- return "values:", f(g())
-- return f(g()), "cutoff"
