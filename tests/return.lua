function abs(n)
    return (n >= 0 and n) or (n * -1)
end

function pow(base, exponent)
    if exponent == 0 then
        return 1
    end
    local x = 1
    if exponent < 0 then
        for i = 1, abs(exponent) do
            x = x / base
        end
    else
        for i = 1, exponent do
            x = x * base
        end
    end
    return x
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
