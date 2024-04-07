#!/usr/bin/env lua

-- Ensure that LUA_PATH is set to lulu's directory!
require "util/common"
require "util/string"

--- Read an entire file's contents to a string array.
---@param filename string
---@return string[]?
local function read_entire_file(filename)
    local handle, ferr = io.open(filename, "rb")
    if not handle then
        io.stderr:write(ferr, '\n')
        return nil
    end
    local contents = string.split(handle:read("*all"), '\n')
    handle:close()
    return contents
end

---Essentially a barebones UNIX `grep` but using Lua pattern matching.
---Mimics `grep --color=never --line-number --regexp=<pattern> -- <filename>`
---@param filename  string Filename to open, read contents of and check.
---@param pattern   string Must be Lua pattern matching string.
local function match_pattern_in_file(filename, pattern)
    local contents = read_entire_file(filename)
    if not contents then
        return -- error handled by above function
    end
    io.stdout:write("Searching '", filename, "' for '", pattern, "'.\n")
    for i, line in ipairs(contents) do
        local start, stop = 1, 1
        while true do
            start, stop = line:find(pattern, start)
            if start and stop then
                local where = string.format("%s:%i<%i,%i>:", filename, i, start, stop)
                io.stdout:write(where, line, '\n')
                start = stop + 1
            else 
                break 
            end
        end
    end
end

local function main(argc, argv)
    print(script_path())
    if argc < 2 then
        io.stderr:write("[USAGE]: ", argv[0], " <pattern> <file...>\n")
        os.exit(1) 
    end

    for i = 2, argc, 1 do
        match_pattern_in_file(argv[i], argv[1])
    end
end

main(#arg, arg)
