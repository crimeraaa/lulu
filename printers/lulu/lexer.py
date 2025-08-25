import gdb # type: ignore
from typing import Final, Optional

Token_Type: Final = gdb.lookup_type("Token_Type")

_token_strings, _ = gdb.lookup_symbol("token_strings")
token_strings = _token_strings.value() # type: ignore

mode = {
    Token_Type["TOKEN_IDENT"].enumval:  "ostring",
    Token_Type["TOKEN_STRING"].enumval: "ostring",
    Token_Type["TOKEN_NUMBER"].enumval: "number",
}

class TokenPrinter:
    __type: gdb.Value
    __data: gdb.Value | str

    def __init__(self, token: gdb.Value):
        self.__type = token["type"]
        i = int(self.__type)
        if i in mode:
            self.__data = token[mode[i]]
        else:
            self.__data = token_strings[i].string()

    def to_string(self) -> str:
        if isinstance(self.__data, str):
            # C-style quoting.
            q = '\'' if len(self.__data) == 1 else '\"'
            return f"{q}{self.__data}{q}"

        t = str(self.__type).removeprefix("TOKEN_").lower()
        return f"{t}: {self.__data}"


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
