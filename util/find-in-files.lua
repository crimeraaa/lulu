#!/usr/bin/env lua

-- Ensure that LUA_PATH is set to lulu's directory!
require "util/ansi"
require "util/common"

local set_color    = SGR.set_fg_color
local reset_styles = SGR.reset_styles
local reset_colors = SGR.reset_colors

-- Print matches inline with the rest of the unmatched string. Please ensure
-- that all SGR styles have been reset before calling this function!
---@param subject string
---@param pattern string
local function match_linewise(subject, pattern)
    local start, stop = subject:find(pattern)

    -- If there is an unmatched string to our left, print it out.
    if start > 1 then
        io.stdout:write(subject:sub(1, start - 1))
    end

    while start and stop do
        -- Track the ending index of the current match so we can extract the
        -- start of the unmatched string later.
        local prev = stop

        -- Extract the ONLY the current match.
        local matched = subject:sub(start, stop)

        -- Try to find where the next match will be located.
        start, stop = subject:find(pattern, stop + 1)

        -- Extract the rest of the string sans the next match. If `start` is
        -- `nil` that indicates there's no more matches so we can safely write
        -- out the rest of the string hence we use an ending index of -1.
        local unmatched = subject:sub(prev + 1, (start and start - 1) or -1)

        io.stdout:write(set_color("red", "bold"), matched, reset_styles(), unmatched)
    end
    io.stdout:write('\n')
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
        if string.match(line, pattern) then
            -- Colored preamble, similar to grep with colors enabled
            io.stdout:write(set_color("magenta"), filename,
                            set_color("cyan"),    ':',
                            set_color("green"),   i,
                            set_color("cyan"),    ':',
                            reset_colors())
            match_linewise(line, pattern)
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
