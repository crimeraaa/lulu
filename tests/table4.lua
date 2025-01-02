local t = {k = {v = 13}}

print("Expected:", "table: 0x...", "Actual:", t)
print("Expected:", "table: 0x...", "Actual:", t.k)
print("Expected:", 13, "Actual:", t.k.v)

