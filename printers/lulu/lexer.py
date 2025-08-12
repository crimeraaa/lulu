import gdb # type: ignore
from typing import Final

Token_Type: Final = gdb.lookup_type("Token_Type")

_token_strings, _ = gdb.lookup_symbol("token_strings")
token_strings = _token_strings.value() # type: ignore

mode = {
    Token_Type["TOKEN_IDENT"].enumval:  "ostring",
    Token_Type["TOKEN_STRING"].enumval: "ostring",
    Token_Type["TOKEN_NUMBER"].enumval: "number",
}

class TokenPrinter:
    __type: Final[gdb.Value]
    __data: Final[gdb.Value]

    def __init__(self, token: gdb.Value):
        self.__type  = token["type"]
        if int(self.__type) in mode:
            self.__data = token[mode[int(self.__type)]]
        else:
            self.__data = token_strings[int(self.__type)]

    def to_string(self) -> str:
        ttype = str(self.__type).removeprefix("TOKEN_").lower()
        return f"{ttype}: {self.__data}"


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
