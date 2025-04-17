local x, y = 1, 2

-- Initialize a local in `chunk.locals` and increment `chunk.count_local`
-- Increment `compiler.active_local` then immediately decrement it
do
    local inner
end

do
    local x, y; -- expected: nil, nil
    x, y = y, x -- expected: 2, 1
end
