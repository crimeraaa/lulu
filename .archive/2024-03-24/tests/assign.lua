do
    local i,j=11,22 -- do comma-separated assignments work properly?
    print("expected 11 and 22:",i,j)
end

do
    local i,j -- ensure both are initialized to nil.
    print("expected nil and nil:",i,j)
end

do
    local i,j=11 -- i is initialized to 11, but j is set to nil.
    print("expected 11 and nil:",i,j)
end

do
    local i,j=11,22,33,44,55 -- excess expressions are popped off the stack.
    print("expected 11 and 22:",i,j)
end
