import gdb # type: ignore
from typing import Final

class ExprPrinter:
    """
    ```
    struct lulu::[expr.odin]::Expr [
        enum lulu::[expr.odin]::Expr_Type type;
        struct lulu::[expr.odin]::Expr_Info info;
        int jump_if_true;
        int jump_if_false;
    ]
    ```
    """
    __type: Final[gdb.Value]
    __info: Final[gdb.Value]

    def __init__(self, val: gdb.Value):
        self.__type = val["type"]
        self.__info = val["info"]

    def to_string(self) -> str:
        tag        = str(self.__type)
        info_name  = ""
        info_value = None
        extra      = ""
        match tag:
            case "Discharged":
                info_name  = "register"
                info_value = self.__info["reg"]
            case "Need_Register":
                info_name  = "pc"
                info_value = self.__info["pc"]
            case "Number":
                info_name = "number"
                info_value = self.__info["number"]
            case "Constant":
                info_name = "index"
                info_value = self.__info["index"]
            case "Global":
                info_name  = "index"
                info_value = self.__info["index"]
            case "Local":
                info_name = "reg"
                info_value = self.__info["reg"]
            case "Table_Index":
                info_name  = "table(reg)"
                info_value = self.__info["table"]["reg"]
                extra      = f", key(reg) = {self.__info['table']['index']}"
            case "Jump":
                info_name  = "pc"
                info_value = self.__info["pc"]
            case _:
                return tag

        return f"{tag}: {info_name} = {info_value}" + extra

