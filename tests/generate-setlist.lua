local file = assert(io.open("tests/setlist.lua", "w"))

file:write("-- Generated by ", arg[0], "\n", "local t = {\n")

local alpha  = "abcdefghijklmnopqrstuvwxyz"
local strlen = #alpha

local counter = 1
for line = 1, arg[1] or 10 do
    file:write('\t')
    for col = 1, arg[2] or 10 do
        file:write('\'', alpha:sub(counter, counter), '\'', ",")
        counter = (counter % strlen) + 1
    end
    file:write('\n')
end

file:write("}\n")

assert(io.close(file))
