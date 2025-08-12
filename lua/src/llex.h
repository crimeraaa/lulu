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
#define TOKEN_MAX_LEN	  (sizeof("function") / sizeof(char))


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
typedef enum {
  /* RESERVED */
  TOKEN_AND, TOKEN_BREAK, TOKEN_DO, TOKEN_ELSE, TOKEN_ELSEIF, TOKEN_END,
  TOKEN_FALSE, TOKEN_FOR, TOKEN_FUNCTION, TOKEN_IF, TOKEN_IN, TOKEN_LOCAL,
  TOKEN_NIL, TOKEN_NOT, TOKEN_OR, TOKEN_REPEAT, TOKEN_RETURN, TOKEN_THEN,
  TOKEN_TRUE, TOKEN_UNTIL, TOKEN_WHILE,


  /* BALANCED PAIRS */
  TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN, /* `(` `)` */
  TOKEN_LEFT_CURLY, TOKEN_RIGHT_CURLY, /* `{` `}` */
  TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET, /* `[` `]` */


  /* PUNCTUATION */
  TOKEN_COMMA, TOKEN_COLON, TOKEN_SEMI, /* `,` `:` `;` */
  TOKEN_DOT, TOKEN_CONCAT, TOKEN_VARARG, /* `.` `..` `...` */
  TOKEN_ASSIGN, /* `=` */


  /* ARITHMETIC */
  TOKEN_ADD, TOKEN_SUB, /* `+` `-` */
  TOKEN_MUL, TOKEN_DIV, TOKEN_MOD, /* `*` `/` `%` */
  TOKEN_LEN, TOKEN_POW, /* `#` `^` */


  /* COMPARISON */
  TOKEN_EQ, TOKEN_NEQ, /* `==` `~=` */
  TOKEN_LT, TOKEN_LEQ, /* `<` `<=` */
  TOKEN_GT, TOKEN_GEQ, /* `>` `>=` */


  /* MISC. TERMINALS */
  TOKEN_NUMBER, TOKEN_STRING, TOKEN_NAME, TOKEN_ERROR, TOKEN_EOS
} Token_Type;

/* number of reserved words */
#define NUM_RESERVED    (cast(int, TOKEN_WHILE) + 1)


/* array with token `names' */
LUAI_DATA const char *const luaX_tokens [];


typedef union {
  lua_Number r;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  Token_Type type;
  SemInfo seminfo;
} Token;


typedef struct LexState {
  int character;  /* current character (charint) */
  int errchar;    /* exact character that caused an error to propagate */
  int linenumber; /* input line counter */
  int lastline;   /* line of last token `consumed' */
  Token current;  /* current token */
  Token lookahead;  /* look ahead token */
  struct FuncState *funcstate;  /* `FuncState' is private to the parser */
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
LUAI_FUNC void luaX_lexerror (LexState *lex, const char *msg, Token_Type type);
LUAI_FUNC void luaX_syntaxerror (LexState *lex, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *lex, Token_Type type);


#endif
