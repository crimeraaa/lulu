function makegreeting(greeting, name)
    return greeting .. ' ' .. name .. '!'
end

function sayhi(name)
    print(makegreeting("Hi", name))
end

sayhi("Bobby")
