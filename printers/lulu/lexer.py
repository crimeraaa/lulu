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

