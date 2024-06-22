local x = false -- OP_FALSE ; push false
if x --[[OP_GETLOCAL 0, OP_TEST ]] then
    print("yay")
    -- OP_JUMP ; goto 'end'
elseif x == false then
    print("nay")
    -- OP_JUMP ; goto 'end'
else
    print("huh")
end
