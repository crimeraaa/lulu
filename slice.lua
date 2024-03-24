---@brief   Functions like Python's `slice()`. There are various "overloads".
---         `slice(tbl, start)`:            Similar to `tbl[start:#tbl]`.
---         `slice(tbl, start, stop):       Similar to `tbl[start:stop]`.
---         `slice(tbl, start, stop, step): Similar to `tbl[start:stop:step]`.
---@note    `stop` is always an exclusive 1-based index.
function table.slice(tbl, start, stop, step)
    assert(type(tbl) == "table", "Attempt to slice a non-table value")
    start = start or 1
    stop  = (stop or #tbl) - 1 -- Must represent first index not in slice
    step  = step or 1
    if step == 1 then
        return {unpack(tbl, start, stop)}
    elseif step <= 0 then
        error("Attempt to slice table using step of 0 or less")
    else
        local res = {}
        for i = start, stop, step do
            res[#res + 1] = tbl[i]
        end
        return res
    end
end

function table.array_of_keys(tbl)
    local res = {}
    for k in pairs(tbl) do
        res[#res + 1] = k
    end
    return res
end

local function isarray(tbl)
    local len = #tbl
    local keys = 0
    for _ in pairs(tbl) do
        keys = keys + 1
    end
    return keys == len
end

function dumptable(tbl)
    local iter = (isarray(tbl) and ipairs) or pairs
    for k, v in iter(tbl) do
        local s = string.format("K: %-16s V: %s", tostring(k), tostring(v))
        io.stdout:write(s, '\n')
    end
end
