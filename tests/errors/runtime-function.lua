local x = 0
function f()
    x = x + 1
    null()
    return x
end

-- Upvalue is still open! Do we free it properly?
f()
