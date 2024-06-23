do
    print("---while loop---")
    local a, b, c = 0, 1, 1
    local limit = 10
    while a < limit  do
        print(a)
        a, b, c = b, c, b + c
    end
end

