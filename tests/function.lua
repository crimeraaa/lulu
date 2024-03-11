function sayhi()
    print("Hi")
end

print(clock())
sayhi()
sayhi()

function f()
    print("global!")
end

do
    local function f()
        print("local!");
    end
    f() --> local!
end

f() --> global!

-- Test if passing functions as values works properly.
function caller(callee)
    return "called " .. callee()
end

function callee()
    return "global"
end

print("Expect 'called global': ", caller(callee))

do
    local function callee()
        return "local"
    end
    print("Expect 'called local': ", caller(callee))
end

print("Expect 'called anonymous':", caller(function() return "anonymous" end))

-- Yes this is valid Lua
print("Expect 'argument': ", (function() return "argument" end)())
