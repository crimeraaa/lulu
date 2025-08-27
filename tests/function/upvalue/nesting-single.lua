local function outer()
    local n = -1
    -- Does not explicitly capture 'n', but does so because of 'inner'.
    local function middle()
        print("middle()")
        local function inner()
            n = n + 1
            print("inner(): n =", n)
            return n
        end
        return inner
    end
    print("outer(): n =", n)
    return middle
end

local f = outer() -- n == -1
local g = f() -- n == -1
g() -- n == 0
g() -- n == 1
