local t = {'a', 'b', 'c', 'd'}
-- print("Expected #t:", 4, "Actual #t:", #t)
-- print("Expected v:", 'a', 'b', 'c', 'd', "Actual v:", t[1], t[2], t[3], t[4])

for i, v in ipairs(t) do
    print(string.format("t[%i] = %s", i, v))
end
