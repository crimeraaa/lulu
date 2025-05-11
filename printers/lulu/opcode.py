import gdb # type: ignore
from typing import Final
from enum import Enum

class OpMode(Enum):
    Separate    = 0
    Unsigned_Bx = 1
    Signed_Bx   = 2


opmodes: Final = {
    "Load_Constant": OpMode.Unsigned_Bx,
    "Get_Global":    OpMode.Unsigned_Bx,
    "Set_Global":    OpMode.Unsigned_Bx,
    "Jump":          OpMode.Signed_Bx,
}


class InstructionPrinter:
    """
    ```
    struct lulu::[opcode.odin]::Instruction {
        u16 b : 9;
        u16 c : 9;
        u16 a : 8;
        enum lulu::[opcode.odin]::OpCode op : 6;
    }
    ```
    """
    SIZE_B:  Final = 9
    SIZE_C:  Final = 9
    SIZE_A:  Final = 8
    SIZE_OP: Final = 6
    SIZE_Bx: Final = SIZE_B + SIZE_C

    MAX_Bx:  Final = (1 << SIZE_Bx) - 1
    MAX_sBx: Final = MAX_Bx >> 1

    __b:  int
    __c:  int
    __a:  int
    __op: str

    def __init__(self, ip: gdb.Value):
        self.__b  = int(ip['b'])
        self.__c  = int(ip['c'])
        self.__a  = int(ip['a'])
        self.__op = str(ip["op"])

    def to_string(self) -> str:
        out: list[str] = [f"{self.__op}: A={self.__a}"]
        if self.__op in opmodes:
            if opmodes[self.__op] == OpMode.Signed_Bx:
                out.append(f", sBx={self.__sbx}")
            else:
                out.append(f", Bx={self.__bx}")
        else:
            out.append(f", B={self.__b}, C={self.__c}")

        return ''.join(out)

    @property
    def __bx(self) -> int:
        return (self.__b << self.SIZE_B) | self.__c

    @property
    def __sbx(self) -> int:
        return self.__bx - self.MAX_sBx

