-- Get the factorial of n.
function fact(n)
    if n <= 0 then
        return 1
    end
    local acc = 1 -- accumulator
    -- iterative approach, non-recursive
    for i = n, 1, -1 do
        acc = acc * i
    end
    return acc
end

local t = clock()
local n = fact(4)
t = clock() - t
print("fact(4):", n, "time:", t)
