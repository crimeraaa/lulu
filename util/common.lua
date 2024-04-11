-- Get the current working directory of the calling function.
---@return string
---@note   The returned directory may not necessarily be absolute.
function script_path()
    -- Level 0 means `debug.getinfo`, 1, this function, 2 means the caller.
    -- Need `sub(2)` because of the `@` symbol.
    local dir = debug.getinfo(2, "S").source:sub(2)
    return dir:match("(.*[/\\])") or "./"
end

-- Python-style `range` function which actually returns a Lua iterator function.
-- The returned function is useable only within `for-in` constructs.
---@param start integer Starting inclusive index. Default to 0 if only 1 arg.
---@param stop? integer Ending exclusive index. Default to `start` if only 1 arg.
---@param step? integer Increment. If omitted, defaults to +1.
---@note  See: https://stackoverflow.com/a/73354604
function range(start, stop, step)
    -- for i in range(3)     := for i=0,2,1
    -- for i in range(-1)    := for i=0,-2,1 => should not start
    -- for i in range(1,5,2) := for i=1,4,2
    if not stop then
        start, stop = 0, start
    end
    -- No matter what, if `step` is nil/omitted we default to +1 increment.
    if not step then
        step = 1
    end

    assert(step ~= 0, "range() `step` of 0 will cause an infinite loop.")

    -- If only we didn't rely on the upvalue `step` we could avoid constantly
    -- allocating memory for temporary functions like this by moving it out into
    -- the top-level file scope but as a local.
    ---@param invariant integer
    ---@param control   integer
    local iterfn = function(invariant, control)
        control = control + step
        -- Due to how logical operators short-circuit we need separate checks,
        -- e.g. `(step > 0 and control < invariant) or control > invariant`
        -- would always fall back to the `control > invariant` part.
        if step > 0 then
            if control < invariant then return control end
        else
            if control > invariant then return control end
        end
        return nil
    end
    -- Adjust the control variable because it's increment on the first call.
    return iterfn, stop, start - step
end
