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

-- Split string into individual captures based as separated by `delimiters`.
--
-- Does not include empty values, use `string.split_keep_empty` instead for that.
---@param subject str
---@param delimiters str
---@nodiscard
function string.split_noempty(subject, delimiters)
    local captures = {} ---@type str[]
    local pattern = string.format("[^%s]+", delimiters)
    for capture in subject:gmatch(pattern) do
        captures[#captures+1] = capture
    end
    return captures
end

-- Works for strings with empty values.
-- From user bart at: https://stackoverflow.com/a/7615129
---@param subject str
---@param delimiters str
---@nodiscard
function string.split_keep_empty(subject, delimiters)
    local pattern = string.format("([^%s]*)(%s?)", delimiters, delimiters)
    local captures = {} ---@type str[]
    -- 2 specified capture groups = 2 return values
    for capture, control in subject:gmatch(pattern) do
        captures[#captures + 1] = capture
        if control == "" then -- EOF/no more lines to parse!
            return captures
        end
    end
    return captures
end

-- Creates a string array of based off of `subject`.
-- We separate the strings based on the chars in `separators`.
---@param subject     str
---@param delimiters? str   Defaults to "%s", which splits by whitespace.
---@param keep_empty? bool  use `split_keep_empty` if `true` else `split_noempty`.
---@nodiscard
function string.split(subject, delimiters, keep_empty)
    keep_empty = keep_empty or false
    local fn = (keep_empty and string.split_keep_empty) or string.split_noempty
    return fn(subject, delimiters or "%s")
end
