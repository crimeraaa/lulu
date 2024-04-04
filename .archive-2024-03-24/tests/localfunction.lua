local i=-1

-- Because we use a separate Compiler* for this, it has a new locals stack.
-- As a result, for now, we can't resolve external local variables.
function f()
    i = i + 1
    print(i)
end

f() -- Should throw runtime error for undefined variable
