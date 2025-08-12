local t = {}

t[1] = 'a'
print("Expected #t:", 1, "Actual #t:", #t)
print("Expected v:", 'a', nil, nil, nil, "Actual v:", t[1], t[2], t[3], t[4])

t[2] = 'b'
print("Expected #t:", 2, "Actual #t:", #t)
print("Expected v:", 'a', 'b', nil, nil, "Actual v:", t[1], t[2], t[3], t[4])

t[3] = 'c'
print("Expected #t:", 3, "Actual #t:", #t)
print("Expected v:", 'a', 'b', 'c', nil, "Actual v:", t[1], t[2], t[3], t[4])

t[4] = 'd'
print("Expected #t:", 4, "Actual #t:", #t)
print("Expected v:", 'a', 'b', 'c', 'd', "Actual v:", t[1], t[2], t[3], t[4])
