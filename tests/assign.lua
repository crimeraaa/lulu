do
    local one, two, three = 1, 2, 3
    print("one, two, three:", one, two, three, "; Expected:", 1, 2, 3)
end

do
    local one, two, three
    print("one, two, three:", one, two, three, "; Expected:", nil, nil, nil)
end

do
    local one, two, three = 1, 2
    print("one, two, three:", one, two, three, "; Expected:", 1, 2, nil)
end

do
    -- literal `4` is pushed, but its register is immediately available for reuse
    local one, two, three = 1, 2, 3, 4
    print("one, two, three:", one, two, three, "; Expected:", 1, 2, 3)
end
