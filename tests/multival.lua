local x, y = 1, 2 -- CONSTANT[0], CONSTANT[1]
print(x)          -- GETLOCAL[0], PRINT
print(y)          -- GETLOCAL[1], PRINT
x, y = y, x       -- GETLOCAL[1], GETLOCAL[0], SETLOCAL[1], SETLOCAL[0]
print(x)          -- GETLOCAL[0], PRINT
print(y)          -- GETLOCAL[1], PRINT

outer = nil
do
    local inner
    outer, inner = 11, 22
    print(outer)
    print(inner)
end

do
    local inner
    inner, outer = 100, 20000
    print(outer)
    print(inner)
end

do
    local x, y = y, x
    x, y = (x + 1) * 2, y - 4 ^ 5
    print(x)
    print(y)
end
