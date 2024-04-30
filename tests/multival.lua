print("I:")
local x, y = 1, 2 -- CONSTANT[0], CONSTANT[1]
print(x, y)       -- GETLOCAL[0], GETLOCAL[1], PRINT(ARGC = 2)
x, y = y, x       -- GETLOCAL[1], GETLOCAL[0], SETLOCAL[1], SETLOCAL[0]
print(x, y)       -- GETLOCAL[0], GETLOCAL[1], PRINT(ARGC = 2)


print("II:")
outer = nil
do
    local inner
    outer, inner = 11, 22
    print(outer, inner)
end

print("III:")
do
    local inner
    inner, outer = 100, 20000
    print(outer, inner)
end

print("IV:")
do
    local x, y = y, x
    print(x, y)
    x, y = (x + 1) * 2, y - 4 ^ 5
    print(x, y)
end

print("V:")
do
    local t = {}
    t.k, t.v = 0, 1 -- GETLOCAL 't', CONSTANT 'k'
                    -- GETLOCAL 't', CONSTANT 'v'
                    -- CONSTANT 0,
                    -- CONSTANT 1
                    -- SETTABLE 2 1 ; tbl = base[2], key = base[3], val = top[-1]
                    -- SETTABLE 2 1
    print(t.k, t.v)
    t.k, t.v = t.v, t.k
end
