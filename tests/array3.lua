local t = {}

t[2] = 'b'
print("Expected #t:", 0, "Actual #t:", #t)
print("Expected v:", nil, 'b', "Actual v:", t[1], t[2])

t[1] = 'a'
print("Expected #t:", 2, "Actual #t:", #t)
print("Expected v:", 'a', 'b', "Actual v:", t[1], t[2])
