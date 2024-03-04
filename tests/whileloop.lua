do
    print("-*- WHILE LOOP: BASIC")
    local i = 0
    while (i < 4) do
        print(i)
        i = i + 1
        break
    end
    print("")
end

do
    print("-*- WHILE LOOP: FIBONACCI")
    local x = 0
    local y = 1
    local z = x + y
    while (x < 100) do
        print(x)
        x = y -- Multiple/compound assignment not yet implemented
        y = z
        z = x + y
    end
    print("")
end
