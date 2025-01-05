# Register Allocation
```lua
=1 + 2
```

1. Call `parser_advance()`
    - parser: consumed `<empty>`, lookahead `1`.
    - expr: prev `nil`, type `nil`
    - free_reg: `0`
1. Call `expression()`.
1. Call `parse_precedence(.Assignment + 1)`.
    1. Call `parser_advance()`.
        - parser: consumed `1`, lookahead `+`.
    1. Consumed token has a prefix rule?
        - Yes: Continue.
        - No: Error, expected an expression.
    1. (Yes) Call the prefix rule: `parser.odin:number()`
        1. Assign `expr`.
            - Call `compiler_add_constant()`.
            - expr: prev `nil`, value `1`, data `0`, type `.Literal`
    1. Lookahead token has a higher precedence than consumed?
        - Yes: Continue.
        - No: Do nothing.
    1. (Yes) Call `parser_advance()`.
        - parser: consumed `+`, lookahead `2`.
    1. (Yes) Call the infix rule: `parser.odin:binary()`.
        1. consumed type: `.Plus`
        1. consumed prec: `.Terminal`
        1. allocate stack space for new `Expr` "next": prev `expr`
        1. Call `parse_precedence(.Terminal + 1, next)`
            1. Call `parser_advance()`.
                - parser: consumed `2`, lookahead `<eof>`.
            1. Consumed token has a prefix rule?
                - Yes: Continue.
                - No: Error, Expected an expression.
            1. (Yes) Call the prefix rule: `parser.odin:number()`
                1. Assign `next`.
                    - Call `compiler_add_constant()`.
                    - next: prev `expr`, value `2`, data `1`, type `.Literal`
                        - expr: prev `nil`, value `1`, data `0`, type `.Literal`
            1. Lookahead token has a higher precedence than consumed?
                - Yes: Continue.
                - No: Do nothing.
            1. (No) End this call to `parse_precedence()`.
        1. Determine operator: `Token_Type.Plus` maps to `OpCode.Add`.

