/*
** $Id: llex.c,v 2.20.1.2 2009/11/23 14:58:22 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/


#include <ctype.h>
#include <locale.h>
#include <string.h>

#define llex_c
#define LUA_CORE

#include "lua.h"

#include "ldo.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"

#define next(ls) (ls->character = zgetc(ls->z))
#define currIsNewline(ls)	(ls->character == '\n' || ls->character == '\r')

/* ORDER RESERVED */
const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif", "end",
    "false", "for", "function", "if", "in", "local",
    "nil", "not", "or", "repeat", "return", "then",
    "true", "until", "while",

    "(", ")", "{", "}", "[", "]",                         /* BALANCED PAIRS */
    ",", ":", ";", ".", "..", "...", "=",                 /* PUNCTUATION */
    "+", "-", "*", "/", "%", "#", "^",                    /* ARITHMETIC */
    "==", "~=", "<", "<=", ">", ">=",                     /* COMPARISON */
    "<number>", "<string>", "<name>", "<error>", "<eof>", /* MISC. TERMINALS */
    NULL
};


#define save_and_next(ls) (save(ls, ls->character), next(ls))


#undef next
#undef zgetc

static int zgetc(ZIO *z)
{
  /* Note that we are comparing the value BEFORE the increment. */
  if (z->n-- > 0) {
    return char2int(*z->p++);
  }
  return luaZ_fill(z);
}

static void next (LexState *ls) {
  ls->character = zgetc(ls->z);
}

static void save (LexState *lex, int c) {
  Mbuffer *b = lex->buff;
  if (b->n + 1 > b->buffsize) {
    size_t newsize;
    if (b->buffsize >= MAX_SIZET/2)
      luaX_lexerror(lex, "lexical element too long", Token_Error);
    newsize = b->buffsize * 2;
    luaZ_resizebuffer(lex->L, b, newsize);
  }
  b->buffer[b->n++] = cast(char, c);
}

#undef save_and_next

static void save_and_next (LexState *lex) {
  save(lex, lex->character);
  next(lex->z);
}


void luaX_init (lua_State *L) {
  int i;
  for (i = 0; i < NUM_RESERVED; i++) {
    TString *ts = luaS_new(L, luaX_tokens[i]);
    luaS_fix(ts);  /* reserved words are never collected */
    lua_assert(strlen(luaX_tokens[i])+1 <= TOKEN_LEN);
    ts->tsv.reserved = cast_byte(i+1);  /* reserved word */
  }
}


#define MAXSRC          80


const char *luaX_token2str (LexState *lex, TokenType type) {
  if (type == Token_Error && lex->errchar != -1) {
    return (iscntrl(lex->errchar))
      ? luaO_pushfstring(lex->L, "char(%d)", lex->errchar)
      : luaO_pushfstring(lex->L, "%c", lex->errchar);
  }
  return luaX_tokens[type];
}


static const char *txtToken (LexState *lex, TokenType type) {
  switch (type) {
    case Token_Name:
    case Token_String:
    case Token_Number:
      save(lex, '\0');
      return luaZ_buffer(lex->buff);
    default:
      return luaX_token2str(lex, type);
  }
}


void luaX_lexerror (LexState *lex, const char *msg, TokenType type) {
  char buff[MAXSRC];
  luaO_chunkid(buff, getstr(lex->source), MAXSRC);
  msg = luaO_pushfstring(lex->L, "%s:%d: %s", buff, lex->linenumber, msg);
  /**
   * @note 2025-04-08:
   *  Originally `if (token)` since we used to use charint and `token` could
   *  be an ASCII literal.
   *
   *  With our new system we need to explicitly save the culprit character
   *  beforehand.
   */
  if (type != Token_Error || lex->errchar != -1)
    luaO_pushfstring(lex->L, "%s near " LUA_QS, msg, txtToken(lex, type));
  luaD_throw(lex->L, LUA_ERRSYNTAX);
}


void luaX_syntaxerror (LexState *lex, const char *msg) {
  luaX_lexerror(lex, msg, lex->current.type);
}


TString *luaX_newstring (LexState *lex, const char *str, size_t l) {
  lua_State *L = lex->L;
  TString *ts = luaS_newlstr(L, str, l);
  TValue *o = luaH_setstr(L, lex->funcstate->h, ts);  /* entry for `str' */
  if (ttisnil(o)) {
    setbvalue(o, 1);  /* make sure `str' will not be collected */
    luaC_checkGC(L);
  }
  return ts;
}


static void inclinenumber (LexState *lex) {
  int old = lex->character;
  lua_assert(currIsNewline(lex));
  next(lex);  /* skip `\n' or `\r' */
  if (currIsNewline(lex) && lex->character != old)
    next(lex);  /* skip `\n\r' or `\r\n' */
  if (++lex->linenumber >= MAX_INT)
    luaX_syntaxerror(lex, "chunk has too many lines");
}


void luaX_setinput (lua_State *L, LexState *lex, ZIO *z, TString *source) {
  lex->errchar = -1;
  lex->decpoint = '.';
  lex->L = L;
  lex->lookahead.type = Token_Eos;  /* no look-ahead token (at first) */
  lex->z = z;
  lex->funcstate = NULL;
  lex->linenumber = 1;
  lex->lastline = 1;
  lex->source = source;
  luaZ_resizebuffer(lex->L, lex->buff, LUA_MINBUFFER);  /* initialize buffer */
  next(lex);  /* read first char */
}



/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/



static bool check_next (LexState *lex, const char *set) {
  if (!strchr(set, lex->character))
    return false;
  save_and_next(lex);
  return true;
}


static void buffreplace (LexState *lex, char from, char to) {
  size_t n = luaZ_bufflen(lex->buff);
  char *p = luaZ_buffer(lex->buff);
  while (n--)
    if (p[n] == from) p[n] = to;
}


static void trydecpoint (LexState *lex, SemInfo *seminfo) {
  /* format error: try to update decimal point separator */
  struct lconv *cv = localeconv();
  char old = lex->decpoint;
  lex->decpoint = (cv ? cv->decimal_point[0] : '.');
  buffreplace(lex, old, lex->decpoint);  /* try updated decimal separator */
  if (!luaO_str2d(luaZ_buffer(lex->buff), &seminfo->r)) {
    /* format error with correct decimal point: no more options */
    buffreplace(lex, lex->decpoint, '.');  /* undo change (for error message) */
    luaX_lexerror(lex, "malformed number", Token_Number);
  }
}


/* LUA_NUMBER */
static void read_numeral (LexState *lex, SemInfo *seminfo) {
  lua_assert(isdigit(lex->character));
  do {
    save_and_next(lex);
  } while (isdigit(lex->character) || lex->character == '.');
  if (check_next(lex, "Ee"))  /* `E'? */
    check_next(lex, "+-");  /* optional exponent sign */
  while (isalnum(lex->character) || lex->character == '_')
    save_and_next(lex);
  save(lex, '\0');
  buffreplace(lex, '.', lex->decpoint);  /* follow locale for decimal point */
  if (!luaO_str2d(luaZ_buffer(lex->buff), &seminfo->r))  /* format error? */
    trydecpoint(lex, seminfo); /* try to update decimal point separator */
}


static int skip_sep (LexState *lex) {
  int count = 0;
  int s = lex->character;
  lua_assert(s == '[' || s == ']');
  save_and_next(lex);
  while (lex->character == '=') {
    save_and_next(lex);
    count++;
  }
  return (lex->character == s) ? count : (-count) - 1;
}


static void read_long_string (LexState *lex, SemInfo *seminfo, int sep) {
  int cont = 0;
  (void)(cont);  /* avoid warnings when `cont' is not used */
  save_and_next(lex);  /* skip 2nd `[' */
  if (currIsNewline(lex))  /* string starts with a newline? */
    inclinenumber(lex);  /* skip it */
  for (;;) {
    switch (lex->character) {
      case EOZ:
        luaX_lexerror(lex, (seminfo) ? "unfinished long string" :
                                   "unfinished long comment", Token_Eos);
        break;  /* to avoid warnings */
#if defined(LUA_COMPAT_LSTR)
      case '[': {
        if (skip_sep(lex) == sep) {
          save_and_next(lex);  /* skip 2nd `[' */
          cont++;
#if LUA_COMPAT_LSTR == 1
          if (sep == 0)
            luaX_lexerror(lex, "nesting of [[...]] is deprecated", Token_Left_Bracket);
#endif
        }
        break;
      }
#endif
      case ']': {
        if (skip_sep(lex) == sep) {
          save_and_next(lex);  /* skip 2nd `]' */
#if defined(LUA_COMPAT_LSTR) && LUA_COMPAT_LSTR == 2
          cont--;
          if (sep == 0 && cont >= 0) break;
#endif
          goto endloop;
        }
        break;
      }
      case '\n':
      case '\r': {
        save(lex, '\n');
        inclinenumber(lex);
        if (!seminfo) luaZ_resetbuffer(lex->buff);  /* avoid wasting space */
        break;
      }
      default: {
        if (seminfo) save_and_next(lex);
        else next(lex);
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = luaX_newstring(lex, luaZ_buffer(lex->buff) + (2 + sep),
                                     luaZ_bufflen(lex->buff) - 2*(2 + sep));
}


static void read_string (LexState *lex, int del, SemInfo *seminfo) {
  save_and_next(lex);
  while (lex->character != del) {
    switch (lex->character) {
      case EOZ:
        luaX_lexerror(lex, "unfinished string", Token_Eos);
        continue;  /* to avoid warnings */
      case '\n':
      case '\r':
        luaX_lexerror(lex, "unfinished string", Token_String);
        continue;  /* to avoid warnings */
      case '\\': {
        int c;
        next(lex);  /* do not save the `\' */
        switch (lex->character) {
          case 'a': c = '\a'; break;
          case 'b': c = '\b'; break;
          case 'f': c = '\f'; break;
          case 'n': c = '\n'; break;
          case 'r': c = '\r'; break;
          case 't': c = '\t'; break;
          case 'v': c = '\v'; break;
          case '\n':  /* go through */
          case '\r': save(lex, '\n'); inclinenumber(lex); continue;
          case EOZ: continue;  /* will raise an error next loop */
          default: {
            if (!isdigit(lex->character))
              save_and_next(lex);  /* handles \\, \", \', and \? */
            else {  /* \xxx */
              int i = 0;
              c = 0;
              do {
                c = 10*c + (lex->character-'0');
                next(lex);
              } while (++i<3 && isdigit(lex->character));
              if (c > UCHAR_MAX)
                luaX_lexerror(lex, "escape sequence too large", Token_String);
              save(lex, c);
            }
            continue;
          }
        }
        save(lex, c);
        next(lex);
        continue;
      }
      default:
        save_and_next(lex);
    }
  }
  save_and_next(lex);  /* skip delimiter */
  seminfo->ts = luaX_newstring(lex, luaZ_buffer(lex->buff) + 1,
                                   luaZ_bufflen(lex->buff) - 2);
}


static TokenType set_error(LexState *lex, int ch) {
  lex->errchar = ch;
  return Token_Error;
}


/**
 * @note 2025-04-08:
 *  The following are already accounted for in other cases:
 *    `[` `.` `=` `<` `>`
 */
static TokenType single_char(LexState *lex) {
  switch (lex->character) { /* single-char tokens (+ - / ...) */
    case '(': return Token_Left_Paren;
    case ')': return Token_Right_Paren;
    case '{': return Token_Left_Curly;
    case '}': return Token_Right_Curly;
    case ']': return Token_Right_Bracket;

    case ',': return Token_Comma;
    case ':': return Token_Colon;
    case ';': return Token_Semi;

    case '+': return Token_Add;
    case '*': return Token_Mul;
    case '/': return Token_Div;
    case '%': return Token_Mod;
    case '#': return Token_Len;
    case '^': return Token_Pow;

    default: return set_error(lex, lex->character);
  }
}


/**
 * @brief 2025-04-08:
 *  This is the main workhorse of the lexer!
 */
static TokenType llex (LexState *lex, SemInfo *seminfo) {
  luaZ_resetbuffer(lex->buff);
  for (;;) {
    switch (lex->character) {
      case '\n':
      case '\r': {
        inclinenumber(lex);
        continue;
      }
      case '-': {
        next(lex);
        if (lex->character != '-')
          return Token_Sub;
        /* else is a comment */
        next(lex);
        if (lex->character == '[') {
          int sep = skip_sep(lex);
          luaZ_resetbuffer(lex->buff);  /* `skip_sep' may dirty the buffer */
          if (sep >= 0) {
            read_long_string(lex, NULL, sep);  /* long comment */
            luaZ_resetbuffer(lex->buff);
            continue;
          }
        }
        /* else short comment */
        while (!currIsNewline(lex) && lex->character != EOZ)
          next(lex);
        continue;
      }
      case '[': {
        int sep = skip_sep(lex);
        if (sep >= 0) {
          read_long_string(lex, seminfo, sep);
          return Token_String;
        }
        else if (sep == -1) return Token_Left_Bracket;
        else luaX_lexerror(lex, "invalid long string delimiter", Token_String);
      }
      case '=': {
        next(lex);
        if (lex->character != '=') return Token_Assign;
        else { next(lex); return Token_Eq; }
      }
      case '<': {
        next(lex);
        if (lex->character != '=') return Token_Lt;
        else { next(lex); return Token_Leq; }
      }
      case '>': {
        next(lex);
        if (lex->character != '=') return Token_Gt;
        else { next(lex); return Token_Geq; }
      }
      case '~': {
        next(lex);
        if (lex->character != '=') return set_error(lex, '~');
        else { next(lex); return Token_Neq; }
      }
      case '"':
      case '\'': {
        read_string(lex, lex->character, seminfo);
        return Token_String;
      }
      case '.': {
        save_and_next(lex);
        if (check_next(lex, ".")) {
          if (check_next(lex, ".")) return Token_Vararg;   /* ... */
          else return Token_Concat;   /* .. */
        }
        else if (!isdigit(lex->character)) return Token_Dot; /* . */
        else {
          read_numeral(lex, seminfo);
          return Token_Number;
        }
      }
      case EOZ: {
        return Token_Eos;
      }
      default: {
        if (isspace(lex->character)) {
          lua_assert(!currIsNewline(lex));
          next(lex);
          continue;
        }
        else if (isdigit(lex->character)) {
          read_numeral(lex, seminfo);
          return Token_Number;
        }
        else if (isalpha(lex->character) || lex->character == '_') {
          /* identifier or reserved word */
          TString *ts;
          do {
            save_and_next(lex);
          } while (isalnum(lex->character) || lex->character == '_');
          ts = luaX_newstring(lex, luaZ_buffer(lex->buff),
                                  luaZ_bufflen(lex->buff));
          if (ts->tsv.reserved > 0) { /* reserved word? */
            TokenType type = cast(TokenType, ts->tsv.reserved - 1);
            lua_assert(Token_And <= type && type <= Token_While);
            return type;
          }
          else {
            seminfo->ts = ts;
            return Token_Name;
          }
        }
        else {
          TokenType type = single_char(lex);
          next(lex);
          return type;
        }
      }
    }
  }
}


void luaX_next (LexState *lex) {
  lex->lastline = lex->linenumber;
  if (lex->lookahead.type != Token_Eos) {  /* is there a look-ahead token? */
    lex->current = lex->lookahead;  /* use this one */
    lex->lookahead.type = Token_Eos;  /* and discharge it */
  }
  else {
    lex->current.type = llex(lex, &lex->current.seminfo);  /* read next token */
  }
}


void luaX_lookahead (LexState *lex) {
  lua_assert(lex->lookahead.type == Token_Eos);
  lex->lookahead.type = llex(lex, &lex->lookahead.seminfo);
}

