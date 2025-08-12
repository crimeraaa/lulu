t = {'a','b','c','d'}

for i, v in ipairs(t) do
    print(string.format("t[%i] = %q", i, v))
end
