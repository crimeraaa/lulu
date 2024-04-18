#!/usr/bin/env lua

require "util/common"
local lfs = require "lfs" -- must be installed by LuaRocks!

local SOURCE_DIR = script_path() .. "src/"

local options = {
    ["find"] = {
        cmd = "rg", -- ripgrep
        flags = {"--line-number",
                 "--color=auto",
                 "--heading"},
        default = [[--regexp='\s+$']],
    },
    ["replace"] = {
        cmd = "vim",            -- use vim instead of sed to preserve metadata
        flags = {"-e",          -- run in "Ex" mode
                 "-u NONE",     -- don't load any .vimrc, for speed
                 [[-c ':%s/\v\s+$//ge']],
                 [[-c ':wq']]}
    }
}

local function make_linecmd(what)
    return what.cmd .. ' ' .. table.concat(what.flags, ' ')
end

---@param path string
local function check_whitespace(path)
    ---@type string[]
    local found = {}
    for file in lfs.dir(path) do
        local full = path .. file
        if file ~= '.' and file ~= ".." then
            local handle = io.popen(make_linecmd(find) .. ' ' .. full)
            if handle:read("*all") ~= "" then
                found[#found + 1] = full
            end
            handle:close()
        end
    end
    return found
end

---@param files string[]
local function strip_whitespace(files)
    for _, file in ipairs(files) do
        os.execute(make_linecmd(replace) .. ' ' .. file)
    end
end

strip_whitespace(check_whitespace(SOURCE_DIR))
