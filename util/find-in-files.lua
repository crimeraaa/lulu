#!/usr/bin/env lua

-- Ensure that LUA_PATH is set to lulu's directory!
require "util/ansi"
require "util/common"
require "util/string"

-- Prints the contents of `line` with individual matches highlighted in red.
---@param start   integer   Starting index of the match.
---@param stop    integer   Ending index of the match.
---@param line    string    The entire line to print get matches through.
---@param pattern string    Lua pattern to match.
local function print_matches_inline(start, stop, line, pattern)
    -- Stupid but we print the left side if it doesn't match, if it does
    -- match, start will be 1 and line:sub(1,0) will be empty.
    io.stdout:write(SGR:reset_colors(), line:sub(1, start - 1))
    
    while start and stop do
        -- Save for later so we can extract the `rest` substring.
        local prev = stop
        
        -- Extract the match and the match only, nothing else.
        local match = line:sub(start, stop)
        io.stdout:write(SGR:set_fg_color("red", "bold"), match)

        start, stop = line:find(pattern, stop + 1)
        
        -- This is the unmatched string. If `start` is nil, match until
        -- the end of the string as -1 is shorthand for the last index.
        local rest = line:sub(prev + 1, (start or 0) - 1)
        io.stdout:write(SGR:reset_styles(), rest) 
    end
    io.stdout:write(SGR:reset_colors(), '\n')
end

---Essentially a barebones UNIX `grep` but using Lua pattern matching.
---Mimics `grep --color=always --line-number --regexp=<pattern> -- <filename>`
---@param filename  string Filename to open, read contents of and check.
---@param pattern   string Must be Lua pattern matching string.
local function match_pattern_in_file(filename, pattern)
    local handle, ferr = io.open(filename, "rb")
    if not handle then
        io.stderr:write(ferr, '\n')
        return
    end

    local i = 1 -- line number for printout

    -- We somewhat save memory by not loading into an array beforehand.
    for line in handle:lines("*line") do
        ---@type integer|nil, integer|nil
        local start, stop = line:find(pattern)
        if start and stop then
            -- Colored preamble, similar to grep with colors enabled
            io.stdout:write(SGR:set_fg_color("magenta"), filename, 
                            SGR:set_fg_color("cyan"), ':',
                            SGR:set_fg_color("green"), i,
                            SGR:set_fg_color("cyan"), ':')
            print_matches_inline(start, stop, line, pattern)
        end
        i = i + 1
    end
    handle:close()
end

local function main(argc, argv)
    if argc < 2 then
        io.stderr:write("[USAGE]: ", argv[0], " <pattern> <file...>\n")
        os.exit(1) 
    end

    for i = 2, argc, 1 do
        match_pattern_in_file(argv[i], argv[1])
    end
end

main(#arg, arg)
