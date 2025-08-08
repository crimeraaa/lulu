local t = {}

t[2] = 'b'
print("Expected:", 0, nil, 'b', "Actual:", #t, t[1], t[2])

t[1] = 'a'
print("Expected:", 2, 'a', 'b', "Actual:", #t, t[1], t[2])
