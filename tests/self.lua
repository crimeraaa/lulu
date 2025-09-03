t = {}

function t.f(self)
    print("Hi mom!", self)
end

t:f()

function t:g()
    print("Hi mom!", self)
end

t:g()
-- t:g()
