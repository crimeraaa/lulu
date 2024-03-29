-- To disassemble (full):   ./ChunkSpy.lua --source ./hello.lua
-- To disassemble (brief):  ./ChunkSpy.lua --brief --source ./hello.lua
-- To append to this file:  ./ChunkSpy.lua --source ./hello.lua >> ./hello.lua
do
    print("Hi mom!")
end

--[[
; source chunk: ./hello.lua
; x64 standard (64-bit, little endian, doubles)

; function [0] definition (level 1)
; 0 upvalues, 0 params, 2 stacks
.function  0 0 2 2          ; 0 upvals, 0 params, 2 (is_vararg), 2 (maxstacksize)
.const  "print"             ; Kst(0)
.const  "Hi mom!"           ; Kst(1)
[1] getglobal  0   0        ; R(A) := _G[Kst(Bx)]   'print'
[2] loadk      1   1        ; R(A) := Kst(Bx)       '"Hi mom!"'
[3] call       0   2   1    ; [ R(0) := print ][ R(1) := "Hi mom!" ]
                            ; R(A) := print
                            ; ** arguments **
                            ; R(A+1)   := "Hi mom!"
                            ; R(A+B-1) := R(1)      (last argument position)
                            ; ** return values **
                            ; R(A) := print
                            ; R(A+C-2) := R(-1)     (last return value position)
[4] return     0   1        ; Always emitted by code generation
; end of function
]]
