import gdb # type: ignore
from typing import Final

NO_JUMP: Final = -1

class ExprPrinter:
    __INFO: dict[str, str] = {
        "number":      "number",
        "constant":    "index",
        "global":      "index",
        "local":       "reg",
        "call":        "pc",
        "relocable":   "pc",
        "discharged":  "reg",
        "jump":        "pc",
    }
    __type:  str
    __value: gdb.Value

    def __init__(self, val: gdb.Value):
        self.__value = val
        self.__type  = str(val["type"]).removeprefix("EXPR_").lower()

    def to_string(self) -> str:
        key = self.__INFO[self.__type] if self.__type in self.__INFO else None
        if key:
            return f"{self.__type}: {key}={self.__value[key]}"
        return self.__type

