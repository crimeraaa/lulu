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
  ExprKind_Void,	/* no value */
  ExprKind_Nil,
  ExprKind_True,
  ExprKind_False,
  ExprKind_Constant,		/* info = index of constant in `k' */
  ExprKind_Number,	/* nval = numerical value */
  ExprKind_Local,	/* info = local register */
  ExprKind_Upvalue,       /* info = index of upvalue in `upvalues' */
  ExprKind_Global,	/* info = index of table; aux = index of global name in `k' */
  ExprKind_Index,	/* info = table register; aux = index register (or `k') */
  ExprKind_Jump,		/* info = instruction pc */
  ExprKind_Relocable,	/* info = instruction pc */
  ExprKind_Nonrelocable,	/* info = result register */
  ExprKind_Call,	/* info = instruction pc */
  ExprKind_Vararg	/* info = instruction pc */
} ExprKind;

/**
 * @note 2025-04-07:
 *  Originally called `expdesc`.
 */
typedef struct Expr {
  ExprKind kind;
  union {
    struct { int info, aux; } s;
    lua_Number nval;
  } u;
  int t;  /* patch list (pc) of `exit when true' */
  int f;  /* patch list (pc) of `exit when false' */
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
  struct LexState *lex;  /* lexical state */
  struct lua_State *L;  /* copy of the Lua state */
  struct BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to `ncode') */
  int lasttarget;   /* `pc' of last `jump target' */
  int jpc;  /* list of pending jumps to `pc' */
  int freereg;  /* first free register */
  int nconstants;  /* number of elements in `proto->constants` */
  int nchildren;  /* number of elements in `proto->p` */
  short nlocvars;  /* number of elements in `locvars' */
  lu_byte nactvar;  /* number of active local variables */
  UpValDesc upvalues[LUAI_MAXUPVALUES];  /* upvalues */
  unsigned short actvar[LUAI_MAXVARS];  /* declared-variable stack */
} FuncState;


LUAI_FUNC Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                            const char *name);


#endif
