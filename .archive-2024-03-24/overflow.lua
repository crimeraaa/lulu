-- Testing how deep of a recursive stack we need for lulu to overflow.
-- To test too big of a jump erorrs, use 4096.
STACKSIZE = tonumber(arg[1]) or 16

---@param stream file*
---@param fmt    string
---@param ...    string|number
local function fprintf(stream, fmt, ...)
    stream:write(fmt:format(...))
end

-- Python-style 'range'.
---@param start integer  Starting value of `i`, or the exclusive stop value.
---@param stop  integer? Confusingly, if this is omitted, `start` will be its value.
---@param step  integer? How much to increment/decrement the internal iterator by.
local function range(start, stop, step)
    -- Start at -1 so on first increment we return 0.
    local i = -1
    -- If stop is nil we assume we got only 1 argument, which should actually
    -- represent the stop value.
    if stop == nil then
        stop = start
    else
        i = start - 1
    end
    step = step or 1
    return function()
        i = i + step
        if i < stop then
            return i
        end
        return nil
    end
end

local fp, err = io.open("tests/lmao.lua", "w+")
if err then
    print(err)
    return
end

fprintf(fp, "local i = %i\n", STACKSIZE)
fprintf(fp, "if i == -1 then print(\"Nothing!\")\n")
for i in range(STACKSIZE) do
    fprintf(fp, "elseif i == %i then print('%i')\n", i, i)
end
fprintf(fp, "else print(\"Overflow!\")\n")
fprintf(fp, "end")
