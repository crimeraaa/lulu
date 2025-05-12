x = 13
do
    local x = x + 2
    do
        local x = x * 2
        print("x:", x, "Expected:", 30)
    end
    print("x:", x, "Expected:", 15)
end
print("x", x, "Expected:", 13)
