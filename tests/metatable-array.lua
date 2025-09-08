---@class Array
---@field m_data any[]
---@field m_len integer
Array = {}
Array.mt = nil

function Array.new(data)
    ---@type Array
    local a = setmetatable({m_data={}, m_len=0}, Array.mt)
    local t = a.m_data
    local n = 0
    for i, v in ipairs(data) do
        t[i] = v
        n = n + 1
    end
    a.m_len = n
    return a
end

function Array:at(key)
    if type(key) == "string" then
        return Array[key]
    end
    assert(type(key) == "number")
    return self.m_data[key]
end

function Array:set(index, value)
    assert(type(index) == "number")
    self.m_data[index] = value
end

function Array:push(value)
    local i = self.m_len + 1
    self.m_data[i] = value
    self.m_len = i
end

function Array:len()
    return self.m_len
end

function Array:tostring()
    local s = table.concat(self.m_data, ", ", 1, self.m_len)
    return "Array{"..s.."}"
end

---@type metatable
Array.mt = {
    __index    = Array.at,
    __newindex = Array.set,
    __len      = Array.len,
    __tostring = Array.tostring,
}

local a = Array.new({9, 10, 11})
a[1] = 12
a:push(42)
print("#a=", #a, "a[1]=", a[1])
print("a=", a)
