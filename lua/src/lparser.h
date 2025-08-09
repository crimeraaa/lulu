/*
** $Id: lparser.h,v 1.57.1.1 2007/12/27 13:02:25 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/*
** Expression descriptor
*/

/**
 * @note 2025-04-07:
 *  Originally called `expkind`.
 */
typedef enum {
  EXPR_VOID,	/* no value */
  EXPR_NIL,
  EXPR_TRUE,
  EXPR_FALSE,
  EXPR_CONSTANT,	/* info = index of constant in `k' */
  EXPR_NUMBER,	/* nval = numerical value */
  EXPR_LOCAL,	 /* info = local register */
  EXPR_UPVALUE,  /* info = index of upvalue in `upvalues' */
  EXPR_GLOBAL,  /* info = index of table; aux = index of global name in `k' */
  EXPR_INDEX,	 /* info = table register; aux = index register (or `k') */
  EXPR_JUMP,	/* info = instruction pc ; used for tests/comparisons */
  EXPR_RELOCABLE,	 /* info = instruction pc */
  EXPR_NONRELOCABLE,	/* info = result register */
  EXPR_CALL,	/* info = instruction pc */
  EXPR_VARARG	 /* info = instruction pc */
} Expr_Type;

/**
 * @note 2025-04-07:
 *  Originally called `expdesc`.
 */
typedef struct Expr {
  Expr_Type kind;
  union {
    struct { int info, aux; } s;
    lua_Number nval;
  } u;
  int patch_true;  /* patch list (pc) of `exit when true' */
  int patch_false; /* patch list (pc) of `exit when false' */
} Expr;


/**
 * @note 2025-04-07:
 *  Originally called `upvaldesc`.
 */
typedef struct UpValDesc {
  lu_byte k;
  lu_byte info;
} UpValDesc;


struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
typedef struct FuncState {
  Proto *proto;  /* current function header */
  Table *h;  /* table to find (and reuse) elements in `k' */
  struct FuncState *prev;  /* enclosing function */
  struct LexState *lexstate;  /* lexical state */
  struct lua_State *L;  /* copy of the Lua state */
  struct BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to `ncode') */
  int lasttarget;   /* `pc' of last `jump target' */
  int jpc;  /* list of pending jumps to `pc' */
  int freereg;  /* first free register */
  int nconstants;  /* number of elements in `proto->constants` */
  int nchildren;  /* number of elements in `proto->p` */
  short nlocvars;  /* number of elements in `proto->locvars' */
  lu_byte nactvar;  /* number of active local variables */
  UpValDesc upvalues[LUAI_MAXUPVALUES];  /* upvalues */
  unsigned short actvar[LUAI_MAXVARS];  /* declared-variable stack */
} FuncState;


LUAI_FUNC Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                            const char *name);


#endif
