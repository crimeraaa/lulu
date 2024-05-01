-- Functions like Python's `slice()`. There are various "overloads":
--          `slice(tbl, start)`:            Similar to `tbl[start:#tbl]`.
--          `slice(tbl, start, stop):       Similar to `tbl[start:stop]`.
--          `slice(tbl, start, stop, step): Similar to `tbl[start:stop:step]`.
---@note    `stop` is always an exclusive 1-based index.
function table.slice(tbl, start, stop, step)
    assert(type(tbl) == "table", "Attempt to slice a non-table value")
    start = start or 1
    stop  = (stop or #tbl + 1) - 1 -- Must represent first index not in slice
    step  = step or 1
    if step == 1 then
        return {unpack(tbl, start, stop)}
    elseif step == 0 then
        error("slice() using step of 0 will result in an infinite loop.")
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

function table.is_array(tbl)
    local len = #tbl
    local keys = 0
    for _ in pairs(tbl) do
        keys = keys + 1
    end
    return keys == len
end

function table.dump(tbl, visited, level)
    visited = visited or {} -- Help avoid infinite recursion.
    level   = level or 0
    local iterfn = (table.is_array(tbl) and ipairs) or pairs
    local tab = string.rep('\t', level)
    for k, v in iterfn(tbl) do
        local kvpair = string.format("K: %-16s V: %s", tostring(k), tostring(v))
        io.stdout:write(tab, kvpair, '\n')
        if type(v) == "table" then
            if not visited[v] then
                visited[v] = true
                table.dump(v, visited, level + 1)
            else
                local tab2 = string.rep('\t', level + 1)
                io.stdout:write(tab2, "Visited ", tostring(v), ", skipping.\n")
            end
        end
    end
end

local function copy_recurse(dst, src, isdeep, visited)
    for k, v in pairs(src) do
        if type(v) == "table" and isdeep then
            if not visited[v] then
                visited[v] = true
                dst[k] = {}
                copy_recurse(dst[k], v, isdeep, visited)
            end
        else
            dst[k] = v
        end
    end
end

function table.shallow_copy(src)
    local res = {}
    copy_recurse(res, src, false, {})
    return res
end

function table.deep_copy(src)
    local res = {}
    copy_recurse(res, src, true, {})
    return res
end

function table.copy(src, isdeep)
    return isdeep and table.deep_copy(src) or table.shallow_copy(src)
end
