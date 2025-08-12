local t = {}
local alpha, bravo, charlie = 'a', 'b', 'c'

t.k = {}
t.k.a, t.k['b'], t.k[charlie] = 11, 22, 33
print(t.k[alpha], t.k.b, t.k['c'])

t.k['a'], t.k[bravo], t.k.c = t.k[charlie], t.k[bravo], t.k[alpha]
print(t.k['a'], t.k['b'], t.k['c'])
