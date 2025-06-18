import gdb # type: ignore
from typing import Final

class TokenPrinter:
    __type:   Final[gdb.Value]
    __lexeme: Final[gdb.Value]

    def __init__(self, token: gdb.Value):
        self.__type   = token["type"]
        self.__lexeme = token["lexeme"]

    def to_string(self) -> str:
        ttype = str(self.__type)
        word  = str(self.__lexeme).strip("\'\"")
        quote = '\'' if len(word) == 1 else '\"'
        return f"{ttype}: {quote}{word}{quote}"


class LineInfoPrinter:
    __line:     int
    __start_pc: int
    __end_pc:   int

    def __init__(self, info: gdb.Value):
        self.__line     = int(info["line"])
        self.__start_pc = int(info["start_pc"])
        self.__end_pc   = int(info["end_pc"])

    def to_string(self) -> str:
        return f".code[{self.__start_pc}..={self.__end_pc}] = line {self.__line}"
