-- This is actually about chunk's constants
local s="hi"     -- OP_CONSTANT 'hi'
print(s == "hi") -- OP_GETLOCAL, OP_CONSTANT 'hi'

function compare(s)
    print(s == "hi")
end

compare(s)

print("this is a constant")
print("this is a constant")
