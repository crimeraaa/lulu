local pi, g = 3.14, -9.81

local t = {
    'a',
    gravity = g,
    pi,
    ["zero"] = 0,
}

print("Expected:", "table: 0x...", "Actual:", t)
print("Expected:", 2, "Actual:", #t)
print("Expected:", 'a', "Actual:", t[1])
print("Expected:", g, "Actual:", t.gravity)
print("Expected:", pi, "Actual:", t[2])
print("Expected:", 0, "Actual:", t["zero"])
