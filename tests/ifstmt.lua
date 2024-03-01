local condition = true
local msg -- test assignment from inner blocks
if not condition then
    msg = 'yay'
    print(msg)
else
    msg = 'nay'
    print(msg)
end

if condition then
    local msg = 'yep' -- Test shadowing
    print(msg)
else
    local msg = 'nope'
    print(msg)
end

