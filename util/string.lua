-- Split string into individual captures as separated by `delimiters`.
--
-- Does not include empty values, use `string.split_keep_empty` instead for that.
---@param subject    string
---@param delimiters string
---@nodiscard
function string.split_noempty(subject, delimiters)
    local captures = {} ---@type string[]
    local pattern = string.format("[^%s]+", delimiters)
    for capture in subject:gmatch(pattern) do
        captures[#captures+1] = capture
    end
    return captures
end

-- Works for strings with empty values.
-- From user bart at: https://stackoverflow.com/a/7615129
---@param subject    string
---@param delimiters string
---@nodiscard
function string.split_keep_empty(subject, delimiters)
    local pattern = string.format("([^%s]*)(%s?)", delimiters, delimiters)
    local captures = {} ---@type string[]
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
---@param subject     string
---@param delimiters? string   Defaults to "%s", which splits by whitespace.
---@param keep_empty? boolean  `split_keep_empty` if `true` else `split_noempty`.
---@nodiscard
function string.split(subject, delimiters, keep_empty)
    keep_empty = keep_empty or false
    local fn = (keep_empty and string.split_keep_empty) or string.split_noempty
    return fn(subject, delimiters or "%s")
end

-- Python-style `str.join` function although with more Lua semantics.
-- Instead of using `subject` as the separator you can explicitly specify it.
-- e.g. in Python: `' '.join(["Hi", "mom"])` := `"Hi mom"`
-- but in Lua you need a "subject" string to start with before the list.
---@param subject   string
---@param list      string[]
---@param separator string      If not given, defaults to `' '` (whitespace).
function string.join(subject, list, separator)
    if list and #list > 0 then
        separator = separator or ' '
        return subject .. separator .. table.concat(list, separator)
    else
        return subject
    end
end
