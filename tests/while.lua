local x = 1
while x < 4 do
    -- local is_even = (x % 2) == 0
    -- if is_even then
    if (x % 2) == 0 then
        print(x, "is even")
        break
    end
    print(x, "is odd")
    x = x + 1
end

