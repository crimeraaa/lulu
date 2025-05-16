import gdb # type: ignore
from typing import Final

NO_JUMP: Final = -1

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
    __type:        str
    __info:        gdb.Value
    __patch_true:  int
    __patch_false: int

    def __init__(self, val: gdb.Value):
        self.__type        = str(val["type"])
        self.__info        = val["info"]
        self.__patch_true  = int(val["patch_true"])
        self.__patch_false = int(val["patch_false"])

    def to_string(self) -> str:
        info_name  = ""
        info_value = None
        extra      = ""
        match self.__type:
            case "Discharged":
                info_name  = "reg"
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
                info_name  = "reg"
                info_value = self.__info["reg"]
            case "Table_Index":
                info_name  = "table(reg)"
                info_value = self.__info["table"]["reg"]
                extra      = f", key(reg)={self.__info['table']['index']}"
            case "Jump":
                info_name  = "pc"
                info_value = self.__info["pc"]
            case _:
                pass

        if self.__patch_true != NO_JUMP:
            extra += f", t={self.__patch_true}"
        if self.__patch_false != NO_JUMP:
            extra += f", f={self.__patch_false}"

        if info_name and info_value is not None:
            return f"{self.__type}: {info_name}={info_value}" + extra
        return self.__type + extra

