local t = {'a'}
print("Expected #t:", 1, "Actual #t:", #t)
print("Expected v:", 'a', nil, nil, "Actual v:", t[1], t[2], t[3])
print("Expected:", 1, 'a', nil, nil, "Actual:")

t[3] = 'c'
print("Expected #t:", 1, "Actual #t:", #t)
print("Expected v:", 'a', nil, 'c', "Actual v:", t[1], t[2], t[3])

t[2] = 'b'
print("Expected #t:", 3, "Actual #t:", #t)
print("Expected v:", 'a', 'b', 'c', "Actual v", t[1], t[2], t[3])

