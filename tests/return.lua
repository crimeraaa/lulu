function abs(n)
    return (n >= 0 and n) or (n * -1)
end

function _powpos(base, exp)
    local x = 1
    for i = 1, exp do
        x = x * base
    end
    return x
end

function _powneg(base, exp)
    local x = 1
    for i = 1, abs(exp) do
        x = x / base
    end
    return x
end

function pow(base, exp)
    if exp < 0 then
        return _powneg(base, exp)
    elseif exp > 0 then
        return _powpos(base, exp)
    else
        return 1
    end
end

function fact(n)
    if n <= 0 then
        return 1
    else
        return n * fact(n - 1)
    end
end

print(abs(0))
print(abs(1))
print(abs(-2))
print(abs(-0.3))
print(abs(4.4))

print(pow(2,0))
print(pow(10,3))
print(pow(2,-1))
print(pow(2,-5))

print(fact(0))
print(fact(1))
print(fact(2))
print(fact(3))
