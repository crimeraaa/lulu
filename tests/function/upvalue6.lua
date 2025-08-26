do
    local a = -1
    local function b()
        a = a + 1
        print(a)
    end

    f = b
    b() -- a == 0
end -- OP_CLOSE since we don't return and don't implicitly close open upvals

f() -- a == 1
