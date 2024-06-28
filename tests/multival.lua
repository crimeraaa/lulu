print("I:")
local x, y = 1, 2 -- CONSTANT[0], CONSTANT[1]
print("before:", "x:", x, "y:", y)
-- print(x, y)       -- GETLOCAL[0], GETLOCAL[1], PRINT(ARGC = 2)
x, y = y, x       -- GETLOCAL[1], GETLOCAL[0], SETLOCAL[1], SETLOCAL[0]
-- print(x, y)       -- GETLOCAL[0], GETLOCAL[1], PRINT(ARGC = 2)
print("after:", "x:", x, "y:", y)

print("II:")
outer = nil
print("outer:", outer)
do
    local inner
    outer, inner = 11, 22
    print("outer:", outer, "inner:", inner)
end

print("III:")
print("outer:", outer)
do
    local inner
    inner, outer = 100, 20000
    print("outer:", outer, "inner:", inner)
end

print("IV:")
do
    local x, y = y, x
    print("before", "x:", x, "y:", y)
    x, y = (x + 1) * 2, y - 4 ^ 5
    print("after", "x:", x, "y:", y)
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
    print("before", "t.k:", t.k, "t.v:", t.v)
    t.k, t.v = t.v, t.k
    print("after", "t.k:", t.k, "t.v:", t.v)
end
