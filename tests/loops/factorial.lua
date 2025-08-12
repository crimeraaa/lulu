do
    print("---for loop---")
    local fact, limit = 1, 10
    for i = 1, limit do
        fact = fact * i
    end
    print(fact)
end
