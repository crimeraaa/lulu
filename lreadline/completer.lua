-- The following is a helper script because doing heavy table manipulation
-- from C is a pain.

---@class Completer
---@field env table
---@overload fun(env: table): Completer
local Completer = {}

local LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for",
    "function", "if", "in", "local", "nil", "not", "or", "return", "repeat",
    "then", "true", "until", "while",
}

local type     = type
local require  = require
local tostring = tostring

local format, rep    = string.format, string.rep
local remove         = table.remove
local pairs, ipairs  = pairs, ipairs
local rawget, rawset = rawget, rawset
local getmetatable, setmetatable = getmetatable, setmetatable

local function toqstring(v)
    return type(v) == "string" and format("%q", v) or tostring(v)
end

function Completer.lua(env)
    local c = Completer(env or getfenv())
    for _, keyword in ipairs(LUA_KEYWORDS) do
        c:insert(keyword)
    end
    return require "readline".set_completer(c)
end

---@param text string
function Completer:find(text)
    local first = text:sub(1, 1)
    
    ---@type string[]?
    local list = self[first]
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

    ---@cast text string
    local list, index = self:find(text)
    -- Already exists
    if list and index then
        return
    end

    -- This character isn't mapped yet?
    if not list then
        list = {}
        self[text:sub(1, 1)] = list
    end

    list[#list + 1] = text
end

---@param text string
function Completer:remove(text)
    local list, index = self:find(text)
    if index then
        remove(list, index)
    end
end

---@param env table
function Completer:watch_env(env)
    assert(getmetatable(env) == nil,
       "Pre-existing metatable found; new metatable may not work properly")
    self.env = env
    for k in pairs(env) do
        self:insert(k)
    end

    ---@type metatable
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
    assert(self.env, "No previous environment to restore with")
    local env = setmetatable(self.env)
    self.env = nil
    return env
end

---@param prefix? string
function Completer:dump(prefix)
    if prefix then
        dump_table(self[prefix] or {})
    else
        dump_table(self)
    end
end

local saved_tabs = {[0] = ""}

function dump_table(t, recurse, visited)
    recurse = recurse or 0
    visited = visited or {}

    if visited[t] then
        return
    end
    visited[t] = true

    local tabs = saved_tabs[recurse]
    if not tabs then
        tabs = rep('\t', recurse)
        saved_tabs[recurse] = tabs
    end

    for k, v in pairs(t) do
        io.stdout:write(tabs, '[', toqstring(k), "] = ", toqstring(v), '\n')
        if type(v) == "table" then
            dump_table(v, recurse + 1, visited)
        end
    end
end

---@type metatable
local mt = {__index = Completer}

return setmetatable(Completer, {
    __call = function(self, env)
        -- Each key is letter is in the charset `[a-zA-Z_]`
        ---@type Completer
        local c = setmetatable({}, mt)
        c:watch_env(env or {})
        return c
    end,
})
