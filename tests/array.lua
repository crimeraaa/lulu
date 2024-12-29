do
    local t = {'a', 'b', 'c'}
    print(t, "Expected:", 3, "Got:", #t)
end

do
    local t = {}
    t[2] = 'b'
    print(t, "Expected:", 0, "Got:", #t)
    t[1] = 'a'
    print(t, "Expected:", 2, "Got:", #t)
    print(t[1], t[2])
end

do
    local t = {'a'}
    print(t, "Expected:", 1, #t)
    t[3] = 'c'
    print(t, "Expected:", 1, #t)
    t[2] = 'b'
    print(t, "Expected:", 3, #t)
end
