-- The following is a helper script because doing heavy table manipulation
-- from C is a pain.
local Completer = {}

local LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "if", "in", "local", "nil", "not", "or", "return", "repeat",
    "then", "true", "until", "while",
}

function Completer.lua(env)
    local c = Completer(env)
    for _, keyword in ipairs(LUA_KEYWORDS) do
        c:insert(keyword)
    end
    require "readline".set_completer(c)
    return c
end

function Completer:find(text)
    local first = text:sub(1, 1)
    local list  = self.nodes[first]
    if not list then
        return nil, nil
    end

    for i, v in ipairs(list) do
        if text == v then
            return list, i
        end
    end

    return list, nil
end

function Completer:insert(text)
    if type(text) ~= "string" or #text == 0 then
        return
    end

    local list, index = self:find(text)
    -- Already exists
    if list and index then
        return
    end

    -- This character isn't mapped yet?
    if not list then
        list = {}
        self.nodes[text:sub(1, 1)] = list
    end

    list[#list + 1] = text
end

function Completer:remove(text)
    local list, index = self:find(text)
    if index then
        table.remove(list, index)
    end
end

function Completer:watch_static_env(env)
    self.saved = env
    for k in pairs(env) do
        self:insert(k)
    end
    return env
end

function Completer:watch_dynamic_env(env)
    assert(getmetatable(env) == nil)
    self:watch_static_env(env)

    local mt = {}
    function mt.__index(t, k)
        self:insert(k)
        return rawget(t, k)
    end

    function mt.__newindex(t, k, v)
        if v == nil then
            self:remove(k)
        else
            self:insert(k)
        end
        rawset(t, k, v)
    end

    return setmetatable(env, mt)
end

function Completer:restore_env()
    local env = setmetatable(self.saved)
    return env
end

function Completer:dump(prefix)
    if prefix then
        dump_table(self.nodes[prefix] or {})
    else
        dump_table(self.nodes)
    end
end

local mt = {}

function mt.__index(self, k)
    if type(k) == "string" then
        if #k == 1 then
            return self.nodes[k]
        else
            return Completer[k]
        end
    end
    return nil
end

local base_type     = type
local base_tostring = tostring
local format        = string.format

local function tostring(v)
    return base_type(v) == "string" and format("%q", v) or base_tostring(v)
end

function dump_table(t, recurse, visited)
    recurse = recurse or 0
    visited = visited or {}

    if visited[t] then
        return
    end
    visited[t] = true

    local tabs = string.rep('\t', recurse)
    for k, v in pairs(t) do
        io.stdout:write(tabs, '[', tostring(k), "] = ", tostring(v), '\n')
        if type(v) == "table" then
            dump_table(v, recurse + 1, visited)
        end
    end
end

Completer = setmetatable(Completer, {
    __call = function(self, env)
        -- Each key is letter in the charset `[a-zA-Z_]`
        local c = setmetatable({saved = nil, nodes = {}}, mt)
        c:watch_dynamic_env(env or {})
        return c
    end,
})

return Completer
