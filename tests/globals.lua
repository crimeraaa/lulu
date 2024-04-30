PI, G = 3.14, -9.81

print(PI, G)
print(PI == 3.14, G == -9.81)

G = nil
print(G, G == nil)

-- greet = "Hi"
-- whom  = "mom"
greet, whom = "Hi", "mom"
hello = greet .. ' ' .. whom .. '!'
print(hello, hello == "Hi mom!")
