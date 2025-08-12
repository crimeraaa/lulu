function f(x, y)
    return x+y, x*y
end

local a, b = f(9, 10)
print("Expected:", 9+10, 9*10, "Actual:", a, b)
