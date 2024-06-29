# if
## Lua
```lua
local x
if x then
    print('y')
end
```
## Bytecode
```c
.const
[0]     'x'     // string
[1]     'y'     // string

.code
//  ADDRESS  LINE   OP          ARGS        INFO                STACK
    [0x0000]    1   NIL         1                               { nil }
if_cond:
    [0x0002]    2   GETLOCAL    0           push(.stack[0])     { nil, nil }
    [0x0004]    |   TEST                    if Top[-1]
                |                               goto .if_block
    [0x0005]    |   JUMP        0xffffff    goto .else_block    { nil, nil }
.if_block:
    [0x0009]    3   POP         1           pop(.if_cond)       { nil }
    [0x000b]    |   CONSTANT    1           push(.const[1])     { nil, 'y' }
    [0x000f]    |   PRINT       1                               { nil }
.else_block:
.end:
```

## if-elseif-else
```lua
local x = 10
if x == true then
    print('y')
elseif x == false then
    print('n')
else
    print('?')
end
```

Lulu bytecode:
```c
.const
[0]     'x'     ; string
[1]     10      ; number
[2]     'y'     ; string
[3]     'n'     ; string
[4]     '?'     ; string

.code
    [0x0000]    1   OP_CONSTANT     1       ; '10'
.if_cond:
    [0x0004]    2   OP_GETLOCAL     0       ; local x
    [0x0006]    |   OP_TRUE
    [0x0007]    |   OP_EQ
    [0x0008]    |   OP_TEST                 ; if Top[-1] goto <if_block>
    [0x0009]    |   OP_JUMP         0xffff  ; [PATCH WHEN FALSE] goto <elseif_cond>
.if_block:
    [0x000d]    3   OP_POP          1       ; pop <if_cond> when truthy
    [0x000f]    |   OP_CONSTANT     2       ; 'y'
    [0x0013]    |   OP_PRINT        1
    [0x0015]    |   OP_JUMP         0xffff  ; [PATCH WHEN TRUE] goto <end>
.elseif_cond:
    [0x0019]    4   OP_POP          1       ; pop <if_cond> when falsy
    [0x001b]    |   OP_GETLOCAL     0       ; local x
    [0x001d]    |   OP_FALSE
    [0x001e]    |   OP_EQ
    [0x001f]    |   OP_TEST                 ; if Top[-1] goto <elseif_block>
    [0x0020]    |   OP_JUMP         0x0009  ; [PATCH WHEN FALSE] goto <else_block>
.elseif_block:
    [0x0024]    5   OP_POP          1       ; pop <elseif_cond> when truthy
    [0x0026]    |   OP_CONSTANT     3       ; 'n'
    [0x002a]    |   OP_PRINT        1
    [0x002c]    |   OP_JUMP         0x0015  ; [PATCH WHEN TRUE] goto <end>
.else_block:
    [0x0030]    7   OP_POP          1       ; pop <elseif_cond> when falsy
    [0x0032]    |   OP_CONSTANT     4       ; '?'
    [0x0036]    |   OP_PRINT        1
.end:
    [0x0038]    8   OP_POP          1       ; [DO PATCH] pop local x
    [0x0040]    9   OP_RETURN
```

```lua
local x
if x then
    print('y')
elseif not x then
    print('n')
end
```

```c
.const
[0]     'x'     // string
[1]     'y'     // string
[2]     'n'     // string

.code:
//  ADDRESS     OP          ARGS        INFO                STACK
    [0x0000]    NIL         1                               { nil }
.if_cond:
    [0x0002]    GETLOCAL    0           push(.stack[0])     { nil, nil }
    [0x0004]    TEST                    if Top[-1] goto .if_block;
    [0x0005]    JUMP        0xffffff    goto .elseif_cond;
.if_block:
    [0x0009]    POP         1           pop(.if_cond);      { nil }
    [0x000b]    CONSTANT    1           push(.const[1])     { nil, 'y' }
    [0x000f]    PRINT       1                               { nil }
    [0x0011]    JUMP        0xffffff    goto .end;
.elseif_cond:
    [0x0015]    POP         1           pop(.if_cond);      { nil }
    [0x0017]    GETLOCAL    0           push(.stack[0])     { nil, nil }
    [0x0019]    NOT                                         { nil, true }
    [0x001a]    TEST                    if Top[-1] goto .elseif_block;
    [0x001b]    JUMP        0xffffff    goto .else_block;
.elseif_block:
    [0x001f]    POP         1           pop(elseif_cond)    { nil }
    [0x0021]
.else_block:
    [0x0000]    POP         1           pop(.elseif_cond)   { nil }
    [0x0002]    CONSTANT    2           push(.stack[2])     { nil, 'n' }
    [0x0006]    PRINT       1                               { nil }
.end:
    [0x002b]    POP         1           pop(.stack[0])      { }
    [0x002d]    RETURN                  return;
```
