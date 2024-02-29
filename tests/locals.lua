do
    local a = 1
    local b = 2
    local c = 3
    local d = 4
    local e = 5
    print((a + b) * c - d / -e)
end

do
    local a = 11
    local b = 22
    local c = a + b
    -- local c = a = b -- Semantics updated so that this is disallowed!
    print(c)
end
