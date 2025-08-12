for i = 0, 4, 2 do
    if (i % 2) == 0 then
        local n = i^2
        print(n)
        if n == 4 then
            break
        end
    end
end
