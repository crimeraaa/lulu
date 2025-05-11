import gdb # type: ignore
from typing import Final

class TokenPrinter:
    """
    ```
    #define LITERAL_TAG union{f64,^OString}

    struct lulu::[lexer.odin]::Token {
        enum lulu::[lexer.odin]::Token_Type type;
        struct string lexeme;
        int line;
        struct LITERAL_TAG literal;
    }
    ```
    """
    __type:   Final[gdb.Value]
    __lexeme: Final[gdb.Value]

    def __init__(self, token: gdb.Value):
        self.__type   = token["type"]
        self.__lexeme = token["lexeme"]

    def to_string(self) -> str:
        ttype = str(self.__type)
        word  = str(self.__lexeme)
        # Keyword or operator?
        if ttype.lower() == word or not word.isalpha():
            return ttype
        return f"{ttype}: {word}"

