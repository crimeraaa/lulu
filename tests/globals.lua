PI, G = 3.14, -9.81

print("PI, G", PI, G, "Expected:", 3.14, -9.81)
print("PI == 3.14", PI == 3.14, "Expected:", true)
print("G == -9.81", G == -9.81, "Expected:", true)

G = nil
print("G", G, "Expected:", nil)
print("G == nil", G == nil, "Expected:", true)

-- greet = "Hi"
-- whom  = "mom"
greet, whom = "Hi", "mom"
hello = greet .. ' ' .. whom .. '!'
print("hello:", hello, "Expected:", "Hi mom!")
print("hello == \"Hi mom!\"", hello == "Hi mom!", "Expected:", true)
