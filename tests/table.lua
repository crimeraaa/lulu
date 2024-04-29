print("I:")
do
    local t = {}
    t.k = {}
    t.k.v = 13
    print(t, t.k, t.k.v)
end

print("II:")
do
    local t = {}
    t['k'] = {}
    t['k']['v'] = 13
    print(t, t['k'], t['k']['v'])
end

print("III:")
do
    local t = {}
    -- t = {}
    t['k'] = {}
    t['k']['v'] = {}
    t['k']['v']['a'] = 13
    print(t, t.k, t.k.v, t.k.v.a)
end

print("IV:")
do
    local t = {k = {v = 13}}
    print(t, t.k, t.k.v)
end
