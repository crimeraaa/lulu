local function make_counter()
    local n = -1
    local function f()
        n = n + 1
        print(n)
    end
    f()
    return f
end

-- n = 0
local f = make_counter()
f() -- n = 1
f() -- n = 2
f() -- n = 3
