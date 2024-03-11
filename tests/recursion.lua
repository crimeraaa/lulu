-- If we want to optimize recursive functions, we need to able to identify when
-- tail-call optimizations are actually possible.
-- function go(nth, accumulator)
--     if nth <= 1 then
--         return accumulator
--     else 
--         -- TODO: When this is called, destroy the calling stack frame... somehow
--         return go(nth - 1, nth * accumulator)
--     end
-- end

-- function fact(n)
--     return go(n, 1)
-- end

-- This is what the tail-call optimized version of fact boils down to.
function fact(n)
    local accumulator = 1
    while n > 1 do
        accumulator = n * accumulator
        n = n - 1
    end
    return accumulator
end

print(fact(2))
