local t = {}
t[8] = 'h' -- rehash: #hash = 8, #array = 0
t[7] = 'g'
t[6] = 'f'
t[5] = 'e'
t[4] = 'd'
t[3] = 'c'
t[2] = 'b'

print(#t) -- 0

t[1] = 'a' -- goes in hash because 1 last slot remaining
print(#t) -- 8

t[9] = 'i' -- rehash: #hash = 0, #array = 8
print(#t) -- 9
