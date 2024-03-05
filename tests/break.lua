local i=0;
while i<3 do
    local ii=i+1
    print(ii)
    i=i+1
end
do
    -- local i = 0
    -- while i < 3 do
    --     local x = 1
    --     local y = 2
    --     if i == 2 then
    --         print("breaking")
    --         print(x + y + i)
    --         break
    --     end
    --     i = i + 1
    -- end
    
    -- local j = -1
    -- while j > -3 do
    --     if j == -3 then
    --         print("breaking (2)")
    --         break
    --     end
    --     j = j - 1
    -- end
end
-- Seems we left something on the stack?
print("stuff")
