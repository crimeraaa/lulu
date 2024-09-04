function print_all(a, b, c, d, e)
    print("a:", a, "b:", b, "c:", c, "d:", d, "e:", e)
end
do
    local a = 1
    local b = 2
    local c = 3
    local d = 4
    local e = 5
    print_all(a, b, c, d, e)
    do
        local a=5
        local b=4
        local c=3
        local d=2
        local e=1
        print_all(a, b, c, d, e)
    end
    print_all(a, b, c, d, e)
    print((a + b) * c - d / -e)
end

do
    local a = 11
    local b = 22
    local c = a + b
    -- local c = a = b -- Semantics updated so that this is disallowed!
    print(c)
end
