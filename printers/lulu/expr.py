import gdb # type: ignore
from typing import Final, Literal

NO_JUMP: Final = -1

class ExprPrinter:
    __INFO: dict[str, str] = {
        "number":      "number",
        "constant":    "index",
        "global":      "index",
        "local":       "reg",
        "indexed":     "table",
        "relocable":   "pc",
        "discharged":  "reg",
        "jump":        "pc",
        "call":        "pc",
    }
    __type:  str
    __value: gdb.Value

    def __init__(self, val: gdb.Value):
        self.__value = val
        self.__type  = str(val["type"]).removeprefix("EXPR_").lower()

    def to_string(self) -> str:
        key = self.__INFO[self.__type] if self.__type in self.__INFO else None
        s = self.__type
        if key:
            s = f"{self.__type}: "
            if key == "indexed":
                s += f"{self.__table('reg')}, {self.__table('field_rk')}"
            else:
                s += f"{key}={self.__value[key]}"

        s += self.__patch("patch_true")
        s += self.__patch("patch_false")
        return s

    def __table(self, key: Literal["reg", "field_rk"]) -> str:
        v = self.__value["table"][key]
        return f"{key}={v}"

    def __patch(self, patch: Literal["patch_true", "patch_false"]) -> str:
        pc = self.__value[patch]
        if pc != -1:
            return f", {patch}={pc}"
        return ""

