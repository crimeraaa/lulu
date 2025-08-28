function fact(n)
    -- 0! and 1!
    if n <= 1 then
        return 1
    end
    return n + fact(n - 1)
end

for i = 0, 4 do
    -- local n = fact(i)
    print(fact(i))
end
