import gdb # type: ignore

from typing import Final, Literal

from . import lopcodes

NO_JUMP: Final = -1

class ExprPrinter:
    __kind: str
    __expr: gdb.Value

    def __init__(self, expr: gdb.Value):
        # The ExprKind enum is already accounted for within GDB
        self.__kind = str(expr["kind"]).removeprefix("Expr_")
        self.__expr = expr

    def to_string(self) -> str:
        match self.__kind:
            case "Constant":     return self.__set(info = "constant-index")
            case "Number":       return self.__set(nval = True)
            case "Local":        return self.__set(info = "register")
            case "Upvalue":      return self.__set(info = "upvalue-index")
            case "Global":       return self.__set(info = "constant-index")
            case "Index":        return self.__set(info = "table-register", aux = True)
            case "Jump":         return self.__set(info = "pc")
            case "Relocable":    return self.__set(info = "pc")
            case "Nonrelocable": return self.__set(info = "register")
            case "Call":         return self.__set(info = "pc")
            case "Vararg":       return self.__set(info = "pc")
            case _: return self.__set()

    def __set(self, *, info = "", aux = False, nval = False) -> str:
        args = [self.__kind, ": "]
        if info:
            args.append(f"{info} = {int(self.__expr['u']['s']['info'])}")

        if aux:
            data = int(self.__expr['u']['s']['aux'])
            if lopcodes.ISK(data):
                kind = "constant-index"
                data = lopcodes.INDEXK(data)
            else:
                kind = "register"
            args.append(f", {kind} = {data}")

        if nval:
            args.append(str(float(self.__expr['u']['nval'])))

        args.append(self.__jump("patch_true"))
        args.append(self.__jump("patch_false"))
        return "".join(args)

    def __jump(self, patch: Literal["patch_true", "patch_false"]) -> str:
        jump = int(self.__expr[patch])
        return "" if jump == NO_JUMP else f", {patch} = {jump}"


