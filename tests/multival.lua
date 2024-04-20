local x, y = 1, 2 -- CONSTANT[0], CONSTANT[1]
print(x, y)       -- GETLOCAL[0], GETLOCAL[1], PRINT(ARGC = 2)
x, y = y, x       -- GETLOCAL[1], GETLOCAL[0], SETLOCAL[1], SETLOCAL[0]
print(x, y)       -- GETLOCAL[0], GETLOCAL[1], PRINT(ARGC = 2)

outer = nil
do
    local inner
    outer, inner = 11, 22
    print(outer, inner)
end

do
    local inner
    inner, outer = 100, 20000
    print(outer, inner)
end

do
    local x, y = y, x
    print(x, y)
    x, y = (x + 1) * 2, y - 4 ^ 5
    print(x, y)
end
