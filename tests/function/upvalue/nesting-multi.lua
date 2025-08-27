local i, j, k = 9, 10, 11

-- implicitly captures i, j, k due to middle()
local function outer()
    local n = 3
    -- implicitly captures n, i, j, k due to inner()
    local function middle()
        print("middle()")
        -- explicitly captures n, i, j, k
        local function inner()
            print(string.format("inner(): %i variables: i=%i, j=%i, k=%i", n, i, j, k))
        end
        return inner
    end
    print("outer()")
    return middle
end

-- f()
-- outer()()

local f = outer()
local g = f()
g()
