LIMIT = 100
local a, b = 0, 1
for i = 1, LIMIT do
    a, b = b, a + b
    if a == 13 then
        break
    end
end

print("Expected:", 13, "Actual:", a)
