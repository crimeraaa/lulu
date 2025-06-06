function f(x, y)
    return x+y, x*y
end

local x, y = 9, 10
local a, b = f(x, y)
print("Expected:", x+y, x*y, "Actual:", a, b)
