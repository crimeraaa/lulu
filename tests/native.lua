-- Get the 'n'th fibonacci number.
function fib(n)
    if (n < 2) then
        return n
    else
        return fib(n - 2) + fib(n - 1) 
    end
end

local start = clock() -- Builtin analog to C standard 'clock()'
print(fib(5)) -- 'print' is now a builtin function that takes varargs!
print(clock() - start)
