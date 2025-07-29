import gdb # type: ignore

class LocalPrinter:
    __ident:    str
    __start_pc: int
    __end_pc:   int

    def __init__(self, v: gdb.Value):
        self.__ident    = str(v["ident"])
        self.__start_pc = int(v["start_pc"])
        self.__end_pc   = int(v["end_pc"])

    def to_string(self) -> str:
        return f"{self.__ident}: start={self.__start_pc}, end={self.__end_pc}"
