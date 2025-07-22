import gdb # type: ignore

from typing import Final, Literal

from . import lopcodes

NO_JUMP: Final = -1

class ExprPrinter:
    __kind: str
    __expr: gdb.Value

    def __init__(self, expr: gdb.Value):
        # The ExprKind enum is already accounted for within GDB
        self.__kind = str(expr["kind"]).removeprefix("EXPR_")
        self.__expr = expr

    def to_string(self) -> str:
        match self.__kind:
            case "CONSTANT":     return self.__set(info = "index")
            case "NUMBER":       return self.__set(nval = True)
            case "LOCAL":        return self.__set(info = "reg")
            case "UPVALUE":      return self.__set(info = "upval")
            case "GLOBAL":       return self.__set(info = "index")
            case "INDEX":        return self.__set(info = "reg", aux = True)
            case "JUMP":         return self.__set(info = "pc")
            case "RELOCABLE":    return self.__set(info = "pc")
            case "NONRELOCABLE": return self.__set(info = "reg")
            case "CALL":         return self.__set(info = "pc")
            case "VARARG":       return self.__set(info = "pc")
            case _: return self.__set()

    def __set(self, *, info = "", aux = False, nval = False) -> str:
        args = [self.__kind.lower(), ": "]
        if info:
            args.append(f"{info}={int(self.__expr['u']['s']['info'])}")

        if aux:
            data = int(self.__expr['u']['s']['aux'])
            if lopcodes.ISK(data):
                kind = "constant"
                data = lopcodes.INDEXK(data)
            else:
                kind = "reg"
            args.append(f", {kind}={data}")

        if nval:
            args.append(str(float(self.__expr['u']['nval'])))

        self.__jump("patch_true", args)
        self.__jump("patch_false", args)

        # Remove ": " if not needed
        if len(args) == 2:
            args.pop(1)
        return "".join(args)

    def __jump(self, patch: Literal["patch_true", "patch_false"], args: list[str]):
        jump = int(self.__expr[patch])
        if jump != NO_JUMP:
            args.append(f", {patch}={jump}")


