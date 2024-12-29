do
    local t = {'a', 'b', 'c', 'd'}
    print(t, "Expected:", 4, "Got:", #t)
end

do
    local t = {}
    t[1] = 'a'
    t[2] = 'b'
    t[3] = 'c'
    t[4] = 'd'
    print("Expected:", 4, "Got:", #t)
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
    print("Expected:", 1, "Got:", #t)
    t[3] = 'c'
    print("Expected:", 1, "Got:", #t)
    t[2] = 'b'
    print("Expected:", 3, "Got:", #t)
end

do
    local t = {}
    t[1] = 'a'
    t[2] = 'b'
    t[3] = 'c'
    t[4] = 'd'
    print("Expected:", 4, "Got:", #t)
end
