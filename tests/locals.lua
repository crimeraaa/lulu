x = 13
do
    local x = x + 2
    do
        local x = x * 2
        print("Expected:", 30, "Actual:", x)
    end
    print("Expected:", 15, "Actual:", x)
end
print("Expected:", 13, "Actual:", x)
