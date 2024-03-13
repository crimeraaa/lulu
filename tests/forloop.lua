LIMIT = 11
INCREMENT = 4
for i=0, LIMIT, INCREMENT do
    print(i)
end

for i=0, LIMIT, INCREMENT do
    local i=i+1 -- intentional, scoping resolution rules should apply
    local ii=i+2
    local iii=i+3
    print(i + ii + iii)
    LIMIT = LIMIT + 1         -- should not affect local #2 (condition)
    INCREMENT = INCREMENT + 1 -- should not affect local #3 (increment)
end
