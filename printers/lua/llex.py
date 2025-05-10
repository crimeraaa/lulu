import gdb # type: ignore


class TokenPrinter:
    """ NOTE(2025-04-20): Keep track of the field names in `llex.h`! """
    __type:    str
    __seminfo: str | float | None

    def __init__(self, token: gdb.Value):
        # In GDB, enums already have their proper names
        self.__type = str(token["type"])
        seminfo    = token["seminfo"]
        match self.__type:
            case "Token_Number":
                self.__seminfo = float(seminfo["r"])
            case "Token_String":
                # Let `TStringPrinter` handle it
                self.__seminfo = str(seminfo["ts"])
            case "Token_Name":
                self.__seminfo = str(seminfo["ts"])
            case "Token_Eos":
                self.__seminfo = -1
            case _:
                self.__seminfo = None

    def to_string(self) -> str:
        if self.__seminfo:
            return f"{self.__type}: {self.__seminfo}"
        else:
            return self.__type


