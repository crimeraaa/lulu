-- check if register allocation is correct
local s = "expected:";

print("true and false:", true  and false, s, false)
print("false and true:", false and true,  s, false)
print("false and nil: ", false and nil,   s, false)
print("nil and false: ", nil   and false, s, nil)

print("true or false: ", true  or false,  s, true)
print("false or true: ", false or true,   s, true)
print("false or nil:  ", false or nil,    s, nil)
print("nil or false:  ", nil   or false,  s, false)

print("true and false or nil: ", true and false or nil, s, nil)
print("true and nil or false: ", true and nil or false, s, false)
print("false and true or nil: ", false and true or nil, s, nil)
print("false and nil or true: ", false and nil or true, s, true)
print("nil and true or false: ", nil and true or false, s, false)
print("nil and false or true: ", nil and false or true, s, true)
