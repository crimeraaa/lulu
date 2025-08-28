local n = 0

function f()
    n = n + 1
    print(n)
    return n
end

-- -- Ensure this refers to the exact same upvalue as `f()`!
function g()
    n = n + 2
    print(n)
    return n
end


f() -- n = 1
g() -- n = 3
f() -- n = 4
