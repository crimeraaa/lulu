-- GLOBAL DECLARATIONS TESTS ----------------------------------------------- {{{
PI = 3.14 -- 1: OP_CONSTANT 1 '3.14', OP_SETGLOBAL 0 'PI'
print(PI) -- 3: OP_GETGLOBAL 2 'PI', OP_PRINT
-- }}}
