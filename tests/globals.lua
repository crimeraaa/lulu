-- GLOBAL DECLARATIONS TESTS ----------------------------------------------- {{{
PI = 3.14 -- 1: OP_CONSTANT 1 '3.14', OP_SETGLOBAL 0 'PI'
print(PI) -- 3: OP_GETGLOBAL 2 'PI', OP_PRINT
-- }}}
do
    local a = 1
    local b = 2
    local c = 3
    local d = 4
    local e = 5
    print((a + b) * c - d / -e)
end

do
    local a = 11
    local b = 22
    local c = a = b
    print(c)
end
