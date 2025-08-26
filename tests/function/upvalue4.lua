-- Test if multiple closures have separate upvalues.
function make_counter(start)
    start = start and start - 1 or -1

    local function f()
        start = start + 1
        print(start)
        return start
    end

    return f
end

local fdef = make_counter()
local f3 = make_counter(3)

fdef() -- 0
f3()   -- 3
fdef() -- 1
fdef() -- 2
f3()   -- 4
