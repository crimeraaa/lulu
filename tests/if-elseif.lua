-- This program should print "huh"
local x
if x == true then
    local s = "yay"
    print("s:", s)
elseif x == false then
    local s = "nay"
    print("s:", s)
elseif x == 1 then
    local s = "#1"
    print("s:", s)
else
    local s = "huh"
    print("s:", s)
end
