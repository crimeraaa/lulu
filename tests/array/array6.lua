local t = {'a', 'b', 'c', 'd'}
print("Expected #t:", 4, "Actual #t:", #t)
print("Expected v:", 'a', 'b', 'c', 'd', "Actual v:", t[1], t[2], t[3], t[4])

t[2] = nil
print("Expected #t:", 1, "Actual #t:", #t)
print("Expected v:", 'a', nil, 'c', 'd', "Actual v:", t[1], t[2], t[3], t[4])
