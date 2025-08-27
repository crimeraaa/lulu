-- Python-style range iterator. See also: lib_base.cpp:base_range()
-- Follows Python's `range()` semantics rather than Lua's numeric `for`.
--
---@param start number Required.
---@param stop number?
---@param step number?
---@return function, number, number
local function lua_range(start, stop, step)
    -- for i in range(n)
    if start and not stop then
        start, stop = 0, start
    end
    assert(start and stop, "range start and stop must be non-nil")

    step = step or 1
    assert(step ~= 0, "range step must be non-zero")

    ---@param state number   Invariant state, or loop limit.
    ---@param control number User-facing iterator variable.
    ---@return number?
    local function iterator_fn(state, control)
        control = control + step
        -- for i in range(n, m, 1)
        if step > 0 then
            if control >= state then return end
        -- for i in range(n, m, -1)
        else
            if control <= state then return end
        end
        return control
    end

    -- iterator: iterator_fn
    -- state:    stop
    -- control:  start
    return iterator_fn, stop, start - step
end

local captures = {}
for i in lua_range(4) do
    captures[#captures + 1] = function() print(i); end;
    -- print(i)
end

for _, f in captures do
    f()
end
