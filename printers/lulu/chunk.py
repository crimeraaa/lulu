import gdb # type: ignore

class LocalPrinter:
    __identifier: str
    __start_pc:   int
    __end_pc:     int

    def __init__(self, v: gdb.Value):
        self.__identifier = str(v["identifier"])
        self.__start_pc   = int(v["start_pc"])
        self.__end_pc     = int(v["end_pc"])

    def to_string(self) -> str:
        return f"{self.__identifier}: start={self.__start_pc}, end={self.__end_pc}"
