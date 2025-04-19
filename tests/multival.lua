local x, y = 'x1', 'y1'
print(x, y, "Expected: x1\ty1")

-- Initialize a local in `chunk.locals` and increment `chunk.count_local`
-- Increment `compiler.active_local` then immediately decrement it
do
    local inner
    print(inner, "Expected: nil")
end

do
    local x, y = 'x2', 'y2'; -- expected: x2, y2
    print(x, y, "Expected: x2\ty2")
    x, y = y, x -- expected: y2, x2
    print(x, y, "Expected: y2\tx2")
end

--[[
local x, y = 'x1', 'x2'
do
    local z = 'z1'
end

do
    local x, y
    x, y = x, y
end
 ]]
