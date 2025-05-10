import gdb # type: ignore
from enum import Enum
from typing import Final

SIZE_B:     Final =  9
SIZE_C:     Final =  SIZE_B
SIZE_Bx:    Final =  SIZE_B + SIZE_C
SIZE_A:     Final =  8
SIZE_OP:    Final =  6

MASK_B:     Final =  (1 << SIZE_B)  - 1 # 0b00000000_00000000_00000001_11111111
MASK_C:     Final =  MASK_B
MASK_Bx:    Final =  (1 << SIZE_Bx) - 1 # 0b00000000_00000011_11111111_11111111
MASK_A:     Final =  (1 << SIZE_A)  - 1 # 0b00000000_00000000_00000000_11111111
MASK_OP:    Final =  (1 << SIZE_OP) - 1 # 0b00000000_00000000_00000000_00111111

OFFSET_OP:  Final =  0
OFFSET_A:   Final =  OFFSET_OP + SIZE_OP
OFFSET_C:   Final =  OFFSET_A  + SIZE_A
OFFSET_B:   Final =  OFFSET_C  + SIZE_C
OFFSET_Bx:  Final =  OFFSET_C

MAXARG_Bx:  Final = (1 << SIZE_Bx) - 1
MAXARG_sBx: Final =  MAXARG_Bx >> 1

BITRK: Final = 1 << (SIZE_B - 1) # 0b1_0000_0000

OpCode: Final = gdb.lookup_type("OpCode")


class OpMode(Enum):
    iABC  = 1
    iABx  = 2
    iAsBx = 3


# Special cases; assume all other instructions are in the form iABC
luaP_opmodes: dict[str, OpMode] = {
    "OP_LOADK":     OpMode.iABx,
    "OP_GETGLOBAL": OpMode.iABx,
    "OP_SETGLOBAL": OpMode.iABx,
    "OP_JMP":       OpMode.iAsBx,
    "OP_TFORLOOP":  OpMode.iAsBx,
    "OP_CLOSURE":   OpMode.iABx,
}


def ISK(reg: int) -> bool:
    """ Tests if `reg` represents a constant index rather than a register. """
    return (reg & BITRK) != 0


def INDEXK(reg: int) -> int:
    # Don't use bitwise not `~` because of how Python's big integers work
    return reg & (BITRK - 1)


class InstructionPrinter:
    __ip: int

    def __init__(self, val: gdb.Value):
        # Don't cast `Instruction *` directly to Python `int`
        if val.type.code == gdb.TYPE_CODE_PTR:
            val = val.dereference()

        # `Instruction` is just a typedef for some unsigned 32-bit integer
        self.__ip = int(val)


    def to_string(self) -> str:
        # GDB already represents enums as their names, not their values
        op   = str(self.__op)
        args = [f"{op.removeprefix('OP_')}: A={self.__a}, "]
        if op in luaP_opmodes:
            # The only ones we mapped are iABx or iAsBx
            if luaP_opmodes[op] == OpMode.iABx:
                args.append(f"Bx={self.__bx}")
            else:
                args.append(f"sBx={self.__sbx}")
        else:
            args.append(f"B={self.__b}, C={self.__c}")
        return "".join(args)

    @property
    def __op(self) -> gdb.Value:
        return gdb.Value((self.__ip >> OFFSET_OP) & MASK_OP).cast(OpCode)

    @property
    def __a(self) -> int:
        return (self.__ip >> OFFSET_A) & MASK_A

    @property
    def __b(self) -> int:
        return (self.__ip >> OFFSET_B) & MASK_B

    @property
    def __c(self) -> int:
        return (self.__ip >> OFFSET_C) & MASK_C

    @property
    def __bx(self) -> int:
        return (self.__ip >> OFFSET_Bx) & MASK_Bx

    @property
    def __sbx(self) -> int:
        return self.__bx - MAXARG_sBx
