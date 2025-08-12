local i = 0
while i < 4 do
    if (i % 2) == 0 then
        local n = i ^ 2
        print(n)
        if n == 4 then
            break
        end
    end
    i = i + 2
end
