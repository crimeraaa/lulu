-- ugh
local x = 1
-- local y = 15
while x < 4 do
    local is_even = (x % 2) == 0
    if is_even then
        print(x)
    end
    x = x + 1
end
