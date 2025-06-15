import gdb # type: ignore
from typing import Final
from enum import Enum

class OpCode_Format(Enum):
    ABC  = 0
    ABX  = 1
    ASBX = 2


opmodes: Final = {
    "OP_CONSTANT":   OpCode_Format.ABX,
    "OP_GET_GLOBAL": OpCode_Format.ABX,
    "OP_SET_GLOBAL": OpCode_Format.ABX,
    "OP_JUMP":       OpCode_Format.ASBX,
}

OpCode: Final = gdb.lookup_type("OpCode")


class InstructionPrinter:
    SIZE_B:    Final = 9
    SIZE_C:    Final = 9
    SIZE_A:    Final = 8
    SIZE_OP:   Final = 6
    SIZE_BX:   Final = SIZE_B + SIZE_C

    MAX_B:     Final = (1 << SIZE_B) - 1
    MAX_C:     Final = (1 << SIZE_C) - 1
    MAX_A:     Final = (1 << SIZE_A) - 1
    MAX_OP:    Final = (1 << SIZE_OP) - 1
    MAX_BX:    Final = (1 << SIZE_BX) - 1
    MAX_SBX:   Final = MAX_BX >> 1

    OFFSET_OP: Final = 0
    OFFSET_A:  Final = OFFSET_OP + SIZE_OP
    OFFSET_C:  Final = OFFSET_A + SIZE_A
    OFFSET_B:  Final = OFFSET_C + SIZE_C
    OFFSET_BX: Final = OFFSET_C

    __b:  int
    __c:  int
    __a:  int
    __op: str

    def __init__(self, ip: gdb.Value):
        if ip.type.code == gdb.TYPE_CODE_PTR:
            ip = ip.dereference()

        self.__b  = int((ip >> self.OFFSET_B) & self.MAX_B)
        self.__c  = int((ip >> self.OFFSET_C) & self.MAX_C)
        self.__a  = int((ip >> self.OFFSET_A) & self.MAX_A)
        self.__op = str(((ip >> self.OFFSET_OP) & self.MAX_OP).cast(OpCode))

    def to_string(self) -> str:
        out: list[str] = [f"{self.__op}: A={self.__a}"]
        if self.__op in opmodes:
            if opmodes[self.__op] == OpCode_Format.ASBX:
                out.append(f", sBX={self.__sbx}")
            else:
                out.append(f", BX={self.__bx}")
        else:
            out.append(f", B={self.__b}, C={self.__c}")

        return ''.join(out)

    @property
    def __bx(self) -> int:
        return (self.__b << self.SIZE_B) | self.__c

    @property
    def __sbx(self) -> int:
        return self.__bx - self.MAX_SBX

