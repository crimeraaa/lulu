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

/**
 * @brief
 *  Get the `Instruction *` based on the `expr`'s `pc`.
 *
 * @note
 *  Assumes `expr` is of type `EXPR_RELOCABLE`.
 */
#define getcode(fs, expr)	(&(fs)->proto->code[(expr)->u.s.info])

#define luaK_codeAsBx(fs, op, A, sBx)	\
  luaK_codeABx(fs, op, A, (sBx) + MAXARG_sBx)

#define luaK_setmultret(fs, expr)	luaK_setreturns(fs, expr, LUA_MULTRET)

LUAI_FUNC int luaK_codeABx (FuncState *fs, OpCode o, int A, unsigned int Bx);
LUAI_FUNC int luaK_codeABC (FuncState *fs, OpCode o, int A, int B, int C);
LUAI_FUNC void luaK_fixline (FuncState *fs, int line);
LUAI_FUNC void luaK_nil (FuncState *fs, int from, int n);
LUAI_FUNC void luaK_reserveregs (FuncState *fs, int n);
LUAI_FUNC void luaK_checkstack (FuncState *fs, int n);
LUAI_FUNC int luaK_stringK (FuncState *fs, TString *s);
LUAI_FUNC int luaK_numberK (FuncState *fs, lua_Number r);

/**
 * @brief 2025-04-8:
 *  If possible, transforms `expr` to `EXPR_RELOCABLE` indicating it still
 *  requires a register.
 *
 *  The main exceptions are `Expr_(Local|Call)` which are converted to
 *  `EXPR_NONRELOCABLE`, indicating they have a clearly defined register.
 */
LUAI_FUNC void luaK_dischargevars (FuncState *fs, Expr *expr);

/**
 * @brief 2025-04-08:
 *  Similar to `luaK_exp2nextreg()` but if `expr` is already `EXPR_NONRELOCABLE`
 *  we will not re-emit it.
 */
LUAI_FUNC int luaK_exp2anyreg (FuncState *fs, Expr *expr);

/**
 * @brief 2025-04-08:
 *  Always transforms `expr` to `EXPR_NONRELOCABLE` as it immmediately uses the
 *  next avaiable register (`fs->freereg`) no matter what.
 */
LUAI_FUNC void luaK_exp2nextreg (FuncState *fs, Expr *expr);
LUAI_FUNC void luaK_exp2val (FuncState *fs, Expr *expr);
LUAI_FUNC int luaK_exp2RK (FuncState *fs, Expr *expr);
LUAI_FUNC void luaK_self (FuncState *fs, Expr *expr, Expr *key);
LUAI_FUNC void luaK_indexed (FuncState *fs, Expr *table, Expr *key);
LUAI_FUNC void luaK_goiftrue (FuncState *fs, Expr *expr);
LUAI_FUNC void luaK_storevar (FuncState *fs, Expr *var, Expr *expr);
LUAI_FUNC void luaK_setreturns (FuncState *fs, Expr *expr, int nresults);
LUAI_FUNC void luaK_setoneret (FuncState *fs, Expr *expr);
LUAI_FUNC int luaK_jump (FuncState *fs);
LUAI_FUNC void luaK_ret (FuncState *fs, int first, int nret);
LUAI_FUNC void luaK_patchlist (FuncState *fs, int list, int target);
LUAI_FUNC void luaK_patchtohere (FuncState *fs, int list);
LUAI_FUNC void luaK_concat (FuncState *fs, int *l1, int l2);
LUAI_FUNC int luaK_getlabel (FuncState *func);
LUAI_FUNC void luaK_prefix (FuncState *fs, UnOpr op, Expr *left);
LUAI_FUNC void luaK_infix (FuncState *fs, BinOpr op, Expr *left);
LUAI_FUNC void luaK_posfix (FuncState *fs, BinOpr op, Expr *left, Expr *right);
LUAI_FUNC void luaK_setlist (FuncState *fs, int base, int nelems, int tostore);


#endif
