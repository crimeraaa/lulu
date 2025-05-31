local x
if x == true then
    print("x is true")
elseif x then
    print("x is truthy")
elseif x == false then
    print("x is false")
elseif x == nil then
    print("x is nil")
else
    print("impossible(1)")
end
