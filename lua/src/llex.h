/*
** $Id: llex.h,v 1.58.1.1 2007/12/27 13:02:25 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "lobject.h"
#include "lzio.h"


/* #define FIRST_RESERVED	257 */

/* maximum length of a reserved word */
#define TOKEN_LEN	(sizeof("function")/sizeof(char))


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
typedef enum {
  /* RESERVED */
  Token_And, Token_Break, Token_Do, Token_Else, Token_Elseif, Token_End,
  Token_False, Token_For, Token_Function, Token_If, Token_In, Token_Local,
  Token_Nil, Token_Not, Token_Or, Token_Repeat, Token_Return, Token_Then,
  Token_True, Token_Until, Token_While,


  /* BALANCED PAIRS */
  Token_Left_Paren, Token_Right_Paren, /* `(` `)` */
  Token_Left_Curly, Token_Right_Curly, /* `{` `}` */
  Token_Left_Bracket, Token_Right_Bracket, /* `[` `]` */


  /* PUNCTUATION */
  Token_Comma, Token_Colon, Token_Semi, /* `,` `:` `;` */
  Token_Dot, Token_Concat, Token_Vararg, /* `.` `..` `...` */
  Token_Assign, /* `=` */


  /* ARITHMETIC */
  Token_Add, Token_Sub, /* `+` `-` */
  Token_Mul, Token_Div, Token_Mod, /* `*` `/` `%` */
  Token_Len, Token_Pow, /* `#` `^` */


  /* COMPARISON */
  Token_Eq, Token_Neq, /* `==` `~=` */
  Token_Lt, Token_Leq, /* `<` `<=` */
  Token_Gt, Token_Geq, /* `>` `>=` */


  /* MISC. TERMINALS */
  Token_Number, Token_String, Token_Name, Token_Error, Token_Eos
} TokenType;

/* number of reserved words */
#define NUM_RESERVED    (cast(int, Token_While) + 1)


/* array with token `names' */
LUAI_DATA const char *const luaX_tokens [];


typedef union {
  lua_Number r;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  TokenType type;
  SemInfo seminfo;
} Token;


typedef struct LexState {
  int character;  /* current character (charint) */
  int errchar;    /* exact character that caused an error to propagate */
  int linenumber; /* input line counter */
  int lastline;   /* line of last token `consumed' */
  Token current;  /* current token */
  Token lookahead;  /* look ahead token */
  struct FuncState *func;  /* `FuncState' is private to the parser */
  struct lua_State *L;
  ZIO *z;  /* input stream */
  Mbuffer *buff;  /* buffer for tokens */
  TString *source;  /* current source name */
  char decpoint;  /* locale decimal point */
} LexState;


LUAI_FUNC void luaX_init (lua_State *L);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *lex, ZIO *z, TString *source);
LUAI_FUNC TString *luaX_newstring (LexState *lex, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *lex);
LUAI_FUNC void luaX_lookahead (LexState *lex);
LUAI_FUNC void luaX_lexerror (LexState *lex, const char *msg, TokenType type);
LUAI_FUNC void luaX_syntaxerror (LexState *lex, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *lex, TokenType type);


#endif
