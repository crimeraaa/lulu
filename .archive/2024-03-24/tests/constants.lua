-- This is actually about chunk's constants
local s="hi"     -- OP_CONSTANT 'hi'
print(s == "hi") -- OP_GETLOCAL, OP_CONSTANT 'hi'

function compare(s)
    print(s == "hi")
end

function get_truthiness(v)
    if v then
        print(v, true)
    else
        print(v, false)
    end
end

compare(s)

print("this is a constant")
print("this is a constant")

get_truthiness(true)
get_truthiness(false)
get_truthiness(nil)
get_truthiness("Hi")
get_truthiness(3.14)
