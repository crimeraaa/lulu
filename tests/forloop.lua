LIMIT = 11
INCREMENT = 4
for i=0, LIMIT, INCREMENT do
    local i=i+1 -- intentional, scoping resolution rules should apply
    local ii=i+2
    local iii=i+3
    print(i + ii + iii)
    LIMIT = LIMIT + 1
end
