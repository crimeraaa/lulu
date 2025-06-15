import gdb # type: ignore
from typing import Final

NO_JUMP: Final = -1

class ExprPrinter:
    __type: str
    __value: gdb.Value

    def __init__(self, val: gdb.Value):
        self.__value = val
        self.__type  = str(val["type"])

    def to_string(self) -> str:
        info_name  = ""
        info_value = None
        extra      = ""
        match self.__type:
            case "EXPR_NUMBER":
                info_name = "number"
                info_value = self.__value["number"]
            case "EXPR_CONSTANT":
                info_name = "index"
                info_value = self.__value["index"]
            case "EXPR_DISCHARGED":
                info_name  = "reg"
                info_value = self.__value["reg"]
            case "EXPR_RELOCABLE":
                info_name  = "pc"
                info_value = self.__value["pc"]
            case _:
                pass

        if info_name and info_value is not None:
            return f"{self.__type}: {info_name}={info_value}" + extra
        return self.__type + extra

