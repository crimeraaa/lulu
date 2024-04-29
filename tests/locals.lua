x = 13
do
    local x = x + 2
    do
        local x = x * 2
        print("Inner:", x)
    end
    print("Middle:", x)
end
print("Outer:", x)
