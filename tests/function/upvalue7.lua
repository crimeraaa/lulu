local i, j, k = 9, 10, 11

function f()
    return function ()
        print(i, j, k)
    end
end

-- f()
f()()
