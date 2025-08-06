function f(x, y, z)
    return x, y, z
end

function g()
    return 9, 10, 21, 42
end

return f(g())
-- return "values:", f(g())
-- return f(g()), "cutoff"
