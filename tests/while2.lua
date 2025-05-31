local N = 2
local x = 1
while x <= N do
    local y = 1
    while y <= N do
        print(x, '+', y, '=', x + y)
        y = y + 1
    end
    x = x + 1
end
