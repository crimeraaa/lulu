local t = {}

t['k'] = {}
t['k']['v'] = {}
t['k']['v']['a'] = 13

print("Expected:", "table: 0x...", "Actual:", t)
print("Expected:", "table: 0x...", "Actual:", t.k)
print("Expected:", "table: 0x...", "Actual:", t.k.v)
print("Expected:", 13, "Actual:", t.k.v.a)
