/*
** $Id: lcode.h,v 1.48.1.1 2007/12/27 13:02:25 roberto Exp $
** Code generator for Lua
** See Copyright Notice in lua.h
*/

#ifndef lcode_h
#define lcode_h

#include "llex.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
#define NO_JUMP (-1)


/*
** grep "ORDER OPR" if you change these enums
*/
typedef enum BinOpr {
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV, OPR_MOD, OPR_POW,
  OPR_CONCAT,
  OPR_NE, OPR_EQ,
  OPR_LT, OPR_LE, OPR_GT, OPR_GE,
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;


typedef enum UnOpr { OPR_MINUS, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;

/* Assumes `expr` is of type `Expr_Relocable` */
#define getcode(func, expr)	((func)->proto->code[(expr)->u.s.info])

#define luaK_codeAsBx(func, op, A, sBx)	luaK_codeABx(func, op, A, (sBx) + MAXARG_sBx)

#define luaK_setmultret(func, expr)	luaK_setreturns(func, expr, LUA_MULTRET)

LUAI_FUNC int luaK_codeABx (FuncState *func, OpCode o, int A, unsigned int Bx);
LUAI_FUNC int luaK_codeABC (FuncState *func, OpCode o, int A, int B, int C);
LUAI_FUNC void luaK_fixline (FuncState *func, int line);
LUAI_FUNC void luaK_nil (FuncState *func, int from, int n);
LUAI_FUNC void luaK_reserveregs (FuncState *func, int n);
LUAI_FUNC void luaK_checkstack (FuncState *func, int n);
LUAI_FUNC int luaK_stringK (FuncState *func, TString *s);
LUAI_FUNC int luaK_numberK (FuncState *func, lua_Number r);

/**
 * @brief 2025-04-8:
 *  If possible, transforms `expr` to `Expr_Relocable` indicating it still
 *  requires a register.
 *
 *  The main exceptions are `Expr_(Local|Call)` which are converted to
 *  `Expr_Nonrelocable`, indicating they have a clearly defined register.
 */
LUAI_FUNC void luaK_dischargevars (FuncState *func, Expr *expr);

/**
 * @brief 2025-04-08:
 *  Similar to `luaK_exp2nextreg()` but if `expr` is already `Expr_Nonrelocable`
 *  we will not re-emit it.
 */
LUAI_FUNC int luaK_exp2anyreg (FuncState *func, Expr *expr);

/**
 * @brief 2025-04-08:
 *  Always transforms `expr` to `Expr_Nonrelocable` as it immmediately uses the
 *  next avaiable register (`func->freereg`) no matter what.
 */
LUAI_FUNC void luaK_exp2nextreg (FuncState *func, Expr *expr);
LUAI_FUNC void luaK_exp2val (FuncState *func, Expr *expr);
LUAI_FUNC int luaK_exp2RK (FuncState *func, Expr *expr);
LUAI_FUNC void luaK_self (FuncState *func, Expr *expr, Expr *key);
LUAI_FUNC void luaK_indexed (FuncState *func, Expr *table, Expr *key);
LUAI_FUNC void luaK_goiftrue (FuncState *func, Expr *expr);
LUAI_FUNC void luaK_storevar (FuncState *func, Expr *var, Expr *expr);
LUAI_FUNC void luaK_setreturns (FuncState *func, Expr *expr, int nresults);
LUAI_FUNC void luaK_setoneret (FuncState *func, Expr *expr);
LUAI_FUNC int luaK_jump (FuncState *func);
LUAI_FUNC void luaK_ret (FuncState *func, int first, int nret);
LUAI_FUNC void luaK_patchlist (FuncState *func, int list, int target);
LUAI_FUNC void luaK_patchtohere (FuncState *func, int list);
LUAI_FUNC void luaK_concat (FuncState *func, int *l1, int l2);
LUAI_FUNC int luaK_getlabel (FuncState *func);
LUAI_FUNC void luaK_prefix (FuncState *func, UnOpr op, Expr *left);
LUAI_FUNC void luaK_infix (FuncState *func, BinOpr op, Expr *left);
LUAI_FUNC void luaK_posfix (FuncState *func, BinOpr op, Expr *left, Expr *right);
LUAI_FUNC void luaK_setlist (FuncState *func, int base, int nelems, int tostore);


#endif
