---@class Set
---@field data table
Set = {}

function Set.new(a)
    ---@type Set
    local s = setmetatable({}, Set_MT)
    local t = {}
    s.data = t
    -- Shallow copy
    for _, v in ipairs(a) do t[v] = true end
    return s
end

function Set.union(a, b)
    local c = Set.new{}
    local t = c.data
    for k in pairs(a.data) do t[k] = true end
    for k in pairs(b.data) do t[k] = true end
    return c
end

function Set.intersection(a, b)
    local c = Set.new{}
    local t = c.data
    ---@cast a Set
    ---@cast b Set
    for k in pairs(a.data) do t[k] = b.data[k] end
    return c
end

function Set:tostring()
    local s = "Set{"
    local n = 0
    for k in pairs(self.data) do
        n = n + 1
        s = s..(n > 1 and ", " or "")..tostring(k)
    end
    return s.."}"
end

Set_MT = {
    __index = Set,
    __add = Set.union,
    __mul = Set.intersection,
    __tostring = Set.tostring,
}

local a = Set.new{9, 10, 21}
local b = Set.new{21, 42}
local c = a * b
print(c)

-- local c = a + b
-- print(a:tostring())
-- print(b:tostring())
-- print(c:tostring())
-- print(a:tostring(), b:tostring(), c:tostring())

-- print((a + b):tostring())
-- print(Set.intersection(a, b):tostring())
