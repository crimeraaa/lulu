do
    print("--WHILE LOOP: BASIC--")
    local i = 0
    while (i < 3) do
        local ii  = i + 2
        local iii = i + 3
        if i + ii + iii == 5 then
            print("should be breaking")
            -- break -- Does not pop the 2 locals
        else
            print("should be continuing")
        end
        i = i + 1
    end
    print("")
end

-- do
--     print("-*- WHILE LOOP: FIBONACCI")
--     local x = 0
--     local y = 1
--     local z = x + y
--     while (x < 100) do
--         print(x)
--         x = y -- Multiple/compound assignment not yet implemented
--         y = z
--         z = x + y
--     end
--     print("")
-- end
