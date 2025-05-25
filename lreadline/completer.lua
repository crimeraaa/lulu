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

local assert        = assert
local type          = type
local require       = require
local tostring      = tostring
local remove        = table.remove
local pairs         = pairs
local ipairs        = ipairs
local rawget        = rawget
local rawset        = rawset
local getfenv       = getfenv
local getmetatable  = getmetatable
local setmetatable  = setmetatable

local function toqstring(v)
    if type(v) == "string" then
        ---@cast v string
        local quote = #v == 1 and '\'' or '\"'
        return quote .. v .. quote
    end
    return tostring(v)
end

---@param env? table
function Completer.lua(env)
    local c = Completer(env or getfenv())
    for _, keyword in ipairs(LUA_KEYWORDS) do
        c:insert(keyword)
    end
    return require "readline".set_completer(c)
end

---@param text string
---@return string[]? list
---@return integer?  index
---@return boolean   ok
function Completer:find(text)
    if type(text) ~= "string" or #text == 0 then
        return nil, nil, false
    end

    local first = text:sub(1, 1)

    ---@type string[]?
    local list = self[first]
    if not list then
        return nil, nil, true
    end

    for i, v in ipairs(list) do
        if text == v then
            return list, i, true
        end
    end

    return list, nil, true
end

---@param text string
function Completer:insert(text)
    local list, index, ok = self:find(text)
    -- Invalid `text` or `text` already exists?
    if not ok or (list and index) then
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
    if list and index then
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

    -- Watch all named variable retrievals.
    function mt.__index(t, k)
        self:insert(k)
        return rawget(t, k)
    end

    -- Watch all named variable declaration or assignment.
    function mt.__newindex(t, k, v)
        -- Assigning the variable at `t[k]` a variable to `nil`?
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
local stdout     = io.stdout

---@param t table
---@param recurse? integer
---@param visited? table<table, boolean>
function dump_table(t, recurse, visited)
    recurse = recurse or 0
    visited = visited or {}

    if visited[t] then
        return
    end
    visited[t] = true

    local tabs = saved_tabs[recurse]
    if not tabs then
        tabs = ('\t'):rep(recurse)
        saved_tabs[recurse] = tabs
    end

    for k, v in pairs(t) do
        stdout:write(tabs, '[', toqstring(k), "] = ", toqstring(v), '\n')
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
