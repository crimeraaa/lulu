-- numeric
for i=0, 2 do     -- 0, 1, 2, 3 (break)
    local i = i*2 -- intentional, scoping resolution rules should apply
    print(i)
end

for i=0,7,3 do   -- 0, 3, 6, 9 (break)
    print(i) 
end

for i=-1,10,4 do -- -1, 3, 7, 11 (break)
    local i=i/2
    print(i)
end
