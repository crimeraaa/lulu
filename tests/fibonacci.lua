do
    local limit = 4
    local a, b, c = 0, 1, 1
    while a < limit  do
        print(a)
        a, b, c = b, c, b + c
    end
end
