/*
** $Id: lcode.c,v 2.25.1.5 2011/01/31 14:53:16 roberto Exp $
** Code generator for Lua
** See Copyright Notice in lua.h
*/


#include <stdlib.h>

#define lcode_c
#define LUA_CORE

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "ltable.h"


#define hasjumps(expr)	((expr)->t != (expr)->f)


static bool isnumeral(Expr *expr) {
  return (expr->kind == Expr_Number && expr->t == NO_JUMP && expr->f == NO_JUMP);
}


void luaK_nil (FuncState *func, int from, int n) {
  Instruction *previous;
  if (func->pc > func->lasttarget) {  /* no jumps to current position? */
    if (func->pc == 0) {  /* function start? */
      if (from >= func->nactvar)
        return;  /* positions are already clean */
    }
    else {
      previous = &func->proto->code[func->pc - 1];
      if (GET_OPCODE(*previous) == OP_LOADNIL) {
        int pfrom = GETARG_A(*previous);
        int pto = GETARG_B(*previous);
        if (pfrom <= from && from <= pto+1) {  /* can connect both? */
          if (from+n-1 > pto)
            SETARG_B(*previous, from+n-1);
          return;
        }
      }
    }
  }
  luaK_codeABC(func, OP_LOADNIL, from, from+n-1, 0);  /* else no optimization */
}


int luaK_jump (FuncState *func) {
  int jpc = func->jpc;  /* save list of jumps to here */
  int j;
  func->jpc = NO_JUMP;
  j = luaK_codeAsBx(func, OP_JMP, 0, NO_JUMP);
  luaK_concat(func, &j, jpc);  /* keep them on hold */
  return j;
}


void luaK_ret (FuncState *func, int first, int nret) {
  luaK_codeABC(func, OP_RETURN, first, nret+1, 0);
}


static int condjump (FuncState *func, OpCode op, int A, int B, int C) {
  luaK_codeABC(func, op, A, B, C);
  return luaK_jump(func);
}


static void fixjump (FuncState *func, int pc, int dest) {
  Instruction *jmp = &func->proto->code[pc];
  int offset = dest-(pc+1);
  lua_assert(dest != NO_JUMP);
  if (abs(offset) > MAXARG_sBx)
    luaX_syntaxerror(func->lex, "control structure too long");
  SETARG_sBx(*jmp, offset);
}


/*
** returns current `pc' and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
int luaK_getlabel (FuncState *func) {
  func->lasttarget = func->pc;
  return func->pc;
}


static int getjump (FuncState *func, int pc) {
  int offset = GETARG_sBx(func->proto->code[pc]);
  if (offset == NO_JUMP)  /* point to itself represents end of list */
    return NO_JUMP;  /* end of list */
  else
    return (pc+1)+offset;  /* turn offset into absolute position */
}


static Instruction *getjumpcontrol (FuncState *func, int pc) {
  Instruction *pi = &func->proto->code[pc];
  if (pc >= 1 && testTMode(GET_OPCODE(*(pi-1))))
    return pi-1;
  else
    return pi;
}


/*
** check whether list has any jump that do not produce a value
** (or produce an inverted value)
*/
static bool need_value (FuncState *func, int list) {
  for (; list != NO_JUMP; list = getjump(func, list)) {
    Instruction i = *getjumpcontrol(func, list);
    if (GET_OPCODE(i) != OP_TESTSET)
      return true;
  }
  return false;  /* not found */
}


static bool patchtestreg (FuncState *func, int node, int reg) {
  Instruction *i = getjumpcontrol(func, node);
  if (GET_OPCODE(*i) != OP_TESTSET)
    return false;  /* cannot patch other instructions */
  if (reg != NO_REG && reg != GETARG_B(*i))
    SETARG_A(*i, reg);
  else  /* no register to put value or register already has the value */
    *i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));

  return true;
}


static void removevalues (FuncState *func, int list) {
  for (; list != NO_JUMP; list = getjump(func, list))
      patchtestreg(func, list, NO_REG);
}


static void patchlistaux (FuncState *func, int list, int vtarget, int reg,
                          int dtarget) {
  while (list != NO_JUMP) {
    int next = getjump(func, list);
    if (patchtestreg(func, list, reg))
      fixjump(func, list, vtarget);
    else
      fixjump(func, list, dtarget);  /* jump to default target */
    list = next;
  }
}


static void dischargejpc (FuncState *func) {
  patchlistaux(func, func->jpc, func->pc, NO_REG, func->pc);
  func->jpc = NO_JUMP;
}


void luaK_patchlist (FuncState *func, int list, int target) {
  if (target == func->pc)
    luaK_patchtohere(func, list);
  else {
    lua_assert(target < func->pc);
    patchlistaux(func, list, target, NO_REG, target);
  }
}


void luaK_patchtohere (FuncState *func, int list) {
  luaK_getlabel(func);
  luaK_concat(func, &func->jpc, list);
}


void luaK_concat (FuncState *func, int *l1, int l2) {
  if (l2 == NO_JUMP) return;
  else if (*l1 == NO_JUMP)
    *l1 = l2;
  else {
    int list = *l1;
    int next;
    while ((next = getjump(func, list)) != NO_JUMP)  /* find last element */
      list = next;
    fixjump(func, list, l2);
  }
}


void luaK_checkstack (FuncState *func, int n) {
  int newstack = func->freereg + n;
  if (newstack > func->proto->maxstacksize) {
    if (newstack >= MAXSTACK)
      luaX_syntaxerror(func->lex, "function or expression too complex");
    func->proto->maxstacksize = cast_byte(newstack);
  }
}


void luaK_reserveregs (FuncState *func, int n) {
  luaK_checkstack(func, n);
  func->freereg += n;
}


static void freereg (FuncState *func, int reg) {
  if (!ISK(reg) && reg >= func->nactvar) {
    func->freereg--;
    lua_assert(reg == func->freereg);
  }
}


static void freeexp (FuncState *func, Expr *expr) {
  if (expr->kind == Expr_Nonrelocable)
    freereg(func, expr->u.s.info);
}


static int addk (FuncState *func, TValue *k, TValue *v) {
  lua_State *L = func->L;
  TValue *idx = luaH_set(L, func->h, k);
  Proto *proto = func->proto;
  int oldsize = proto->size_constants;
  if (ttisnumber(idx)) {
    /**
     * @note 2025-04-07:
     *  Originally contained `&fs->f->k` which is equivalent to our new
     *  `&func->proto->constants`.
     */
    lua_assert(luaO_rawequalObj(&proto->constants[cast_int(nvalue(idx))], v));
    return cast_int(nvalue(idx));
  }
  else {  /* constant not found; create a new entry */
    setnvalue(idx, cast_num(func->nconstants));
    luaM_growvector(L, proto->constants, func->nconstants, proto->size_constants,
                    TValue, MAXARG_Bx, "constant table overflow");
    while (oldsize < proto->size_constants)
      setnilvalue(&proto->constants[oldsize++]);
    setobj(L, &proto->constants[func->nconstants], v);
    luaC_barrier(L, proto, v);
    return func->nconstants++;
  }
}


int luaK_stringK (FuncState *func, TString *s) {
  TValue o;
  setsvalue(func->L, &o, s);
  return addk(func, &o, &o);
}


int luaK_numberK (FuncState *func, lua_Number r) {
  TValue o;
  setnvalue(&o, r);
  return addk(func, &o, &o);
}


static int boolK (FuncState *func, int b) {
  TValue o;
  setbvalue(&o, b);
  return addk(func, &o, &o);
}


static int nilK (FuncState *func) {
  TValue k, v;
  setnilvalue(&v);
  /* cannot use nil as key; instead use table itself to represent nil */
  sethvalue(func->L, &k, func->h);
  return addk(func, &k, &v);
}


void luaK_setreturns (FuncState *func, Expr *expr, int nresults) {
  if (expr->kind == Expr_Call) {  /* expression is an open function call? */
    SETARG_C(getcode(func, expr), nresults+1);
  }
  else if (expr->kind == Expr_Vararg) {
    SETARG_B(getcode(func, expr), nresults+1);
    SETARG_A(getcode(func, expr), func->freereg);
    luaK_reserveregs(func, 1);
  }
}


void luaK_setoneret (FuncState *func, Expr *expr) {
  if (expr->kind == Expr_Call) {  /* expression is an open function call? */
    expr->kind = Expr_Nonrelocable;
    expr->u.s.info = GETARG_A(getcode(func, expr)); /* base of function */
  }
  else if (expr->kind == Expr_Vararg) {
    SETARG_B(getcode(func, expr), 2);
    expr->kind = Expr_Relocable;  /* can relocate its simple result */
  }
}


void luaK_dischargevars (FuncState *func, Expr *expr) {
  switch (expr->kind) {
    case Expr_Local: { /* info is already a local register */
      expr->kind = Expr_Nonrelocable;
      break;
    }
    case Expr_Upvalue: {
      expr->u.s.info = luaK_codeABC(func, OP_GETUPVAL, 0, expr->u.s.info, 0);
      expr->kind = Expr_Relocable;
      break;
    }
    case Expr_Global: {
      expr->u.s.info = luaK_codeABx(func, OP_GETGLOBAL, 0, expr->u.s.info);
      expr->kind = Expr_Relocable;
      break;
    }
    case Expr_Index: {
      freereg(func, expr->u.s.aux);
      freereg(func, expr->u.s.info);
      expr->u.s.info = luaK_codeABC(func, OP_GETTABLE, 0, expr->u.s.info, expr->u.s.aux);
      expr->kind = Expr_Relocable;
      break;
    }
    case Expr_Vararg:
    case Expr_Call: {
      luaK_setoneret(func, expr);
      break;
    }
    default: break;  /* there is one value available (somewhere) */
  }
}


static int code_label (FuncState *func, int A, int b, int jump) {
  luaK_getlabel(func);  /* those instructions may be jump targets */
  return luaK_codeABC(func, OP_LOADBOOL, A, b, jump);
}


static void discharge2reg (FuncState *func, Expr *expr, int reg) {
  luaK_dischargevars(func, expr);
  switch (expr->kind) {
    case Expr_Nil: {
      luaK_nil(func, reg, 1);
      break;
    }
    case Expr_False:  case Expr_True: {
      luaK_codeABC(func, OP_LOADBOOL, reg, expr->kind == Expr_True, 0);
      break;
    }
    case Expr_Constant: {
      luaK_codeABx(func, OP_LOADK, reg, expr->u.s.info);
      break;
    }
    case Expr_Number: {
      luaK_codeABx(func, OP_LOADK, reg, luaK_numberK(func, expr->u.nval));
      break;
    }
    case Expr_Relocable: {
      Instruction *pc = &getcode(func, expr);
      SETARG_A(*pc, reg);
      break;
    }
    case Expr_Nonrelocable: {
      if (reg != expr->u.s.info)
        luaK_codeABC(func, OP_MOVE, reg, expr->u.s.info, 0);
      break;
    }
    default: {
      lua_assert(expr->kind == Expr_Void || expr->kind == Expr_Jump);
      return;  /* nothing to do... */
    }
  }
  expr->u.s.info = reg;
  expr->kind = Expr_Nonrelocable;
}


static void discharge2anyreg (FuncState *func, Expr *expr) {
  if (expr->kind != Expr_Nonrelocable) {
    luaK_reserveregs(func, 1);
    discharge2reg(func, expr, func->freereg-1);
  }
}


static void exp2reg (FuncState *func, Expr *expr, int reg) {
  discharge2reg(func, expr, reg);
  if (expr->kind == Expr_Jump)
    luaK_concat(func, &expr->t, expr->u.s.info);  /* put this jump in `t' list */
  if (hasjumps(expr)) {
    int final;  /* position after whole expression */
    int p_f = NO_JUMP;  /* position of an eventual LOAD false */
    int p_t = NO_JUMP;  /* position of an eventual LOAD true */
    if (need_value(func, expr->t) || need_value(func, expr->f)) {
      int fj = (expr->kind == Expr_Jump) ? NO_JUMP : luaK_jump(func);
      p_f = code_label(func, reg, 0, 1);
      p_t = code_label(func, reg, 1, 0);
      luaK_patchtohere(func, fj);
    }
    final = luaK_getlabel(func);
    patchlistaux(func, expr->f, final, reg, p_f);
    patchlistaux(func, expr->t, final, reg, p_t);
  }
  expr->f = expr->t = NO_JUMP;
  expr->u.s.info = reg;
  expr->kind = Expr_Nonrelocable;
}


void luaK_exp2nextreg (FuncState *func, Expr *expr) {
  luaK_dischargevars(func, expr);
  freeexp(func, expr);
  luaK_reserveregs(func, 1);
  exp2reg(func, expr, func->freereg - 1); /* freereg - 1 is the register we want */
}


int luaK_exp2anyreg (FuncState *func, Expr *expr) {
  luaK_dischargevars(func, expr);
  if (expr->kind == Expr_Nonrelocable) {
    if (!hasjumps(expr))
      return expr->u.s.info;  /* exp is already in a register */

    if (expr->u.s.info >= func->nactvar) {  /* reg. is not a local? */
      exp2reg(func, expr, expr->u.s.info);  /* put value on it */
      return expr->u.s.info;
    }
  }
  luaK_exp2nextreg(func, expr);  /* default */
  return expr->u.s.info;
}


void luaK_exp2val (FuncState *func, Expr *expr) {
  if (hasjumps(expr))
    luaK_exp2anyreg(func, expr);
  else
    luaK_dischargevars(func, expr);
}


int luaK_exp2RK (FuncState *func, Expr *expr) {
  luaK_exp2val(func, expr);
  switch (expr->kind) {
    case Expr_Number:
    case Expr_True:
    case Expr_False:
    case Expr_Nil: {
      if (func->nconstants <= MAXINDEXRK) {  /* constant fit in RK operand? */
        expr->u.s.info = (expr->kind == Expr_Nil)  ? nilK(func) :
                      (expr->kind == Expr_Number) ? luaK_numberK(func, expr->u.nval) :
                                        boolK(func, (expr->kind == Expr_True));
        expr->kind = Expr_Constant;
        return RKASK(expr->u.s.info);
      }
      else break;
    }
    case Expr_Constant: {
      if (expr->u.s.info <= MAXINDEXRK)  /* constant fit in argC? */
        return RKASK(expr->u.s.info);
      else break;
    }
    default: break;
  }
  /* not a constant in the right range: put it in a register */
  return luaK_exp2anyreg(func, expr);
}


/**
 * @brief
 *  Emits the bytecode to set global, local, table, or upvalue variables.
 */
void luaK_storevar (FuncState *func, Expr *var, Expr *expr) {
  switch (var->kind) {
    case Expr_Local: {
      int local = var->u.s.info;
      freeexp(func, expr);
      exp2reg(func, expr, local); /* reuse local register or code OP_MOVE */
      return;
    }
    case Expr_Upvalue: {
      /**
       * @note 2025-04-07:
       *  Originally named `expr`, confusingly enough!
       */
      int reg = luaK_exp2anyreg(func, expr);
      luaK_codeABC(func, OP_SETUPVAL, reg, var->u.s.info, 0);
      break;
    }
    case Expr_Global: {
      int reg   = luaK_exp2anyreg(func, expr);
      int index = var->u.s.info;
      luaK_codeABx(func, OP_SETGLOBAL, reg, index);
      break;
    }
    case Expr_Index: {
      int table = var->u.s.info;
      int key   = var->u.s.aux;
      int value = luaK_exp2RK(func, expr);
      luaK_codeABC(func, OP_SETTABLE, table, key, value);
      break;
    }
    default: {
      lua_assert(0);  /* invalid var kind to store */
      break;
    }
  }
  freeexp(func, expr);
}


void luaK_self (FuncState *func, Expr *expr, Expr *key) {
  /**
   * @note 2025-04-07:
   *  Originally called `func`, confusingly enough!
   */
  int index;
  luaK_exp2anyreg(func, expr);
  freeexp(func, expr);
  index = func->freereg;
  luaK_reserveregs(func, 2); /* reserve `self` and key */
  luaK_codeABC(func, OP_SELF, index, expr->u.s.info, luaK_exp2RK(func, key));
  freeexp(func, key);
  expr->u.s.info = index;
  expr->kind = Expr_Nonrelocable;
}


static void invertjump (FuncState *func, Expr *expr) {
  Instruction *pc = getjumpcontrol(func, expr->u.s.info);
  lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
                                           GET_OPCODE(*pc) != OP_TEST);
  SETARG_A(*pc, !(GETARG_A(*pc)));
}


static int jumponcond (FuncState *func, Expr *expr, int cond) {
  if (expr->kind == Expr_Relocable) {
    Instruction ie = getcode(func, expr);
    if (GET_OPCODE(ie) == OP_NOT) {
      func->pc--;  /* remove previous OP_NOT */
      return condjump(func, OP_TEST, GETARG_B(ie), 0, !cond);
    }
    /* else go through */
  }
  discharge2anyreg(func, expr);
  freeexp(func, expr);
  return condjump(func, OP_TESTSET, NO_REG, expr->u.s.info, cond);
}


void luaK_goiftrue (FuncState *func, Expr *expr) {
  int pc;  /* pc of last jump */
  luaK_dischargevars(func, expr);
  switch (expr->kind) {
    case Expr_Constant:
    case Expr_Number:
    case Expr_True: {
      pc = NO_JUMP;  /* always true; do nothing */
      break;
    }
    case Expr_Jump: {
      invertjump(func, expr);
      pc = expr->u.s.info;
      break;
    }
    default: {
      pc = jumponcond(func, expr, 0);
      break;
    }
  }
  luaK_concat(func, &expr->f, pc);  /* insert last jump in `f' list */
  luaK_patchtohere(func, expr->t);
  expr->t = NO_JUMP;
}


static void luaK_goiffalse (FuncState *func, Expr *expr) {
  int pc;  /* pc of last jump */
  luaK_dischargevars(func, expr);
  switch (expr->kind) {
    case Expr_Nil:
    case Expr_False: {
      pc = NO_JUMP;  /* always false; do nothing */
      break;
    }
    case Expr_Jump: {
      pc = expr->u.s.info;
      break;
    }
    default: {
      pc = jumponcond(func, expr, 1);
      break;
    }
  }
  luaK_concat(func, &expr->t, pc);  /* insert last jump in `t' list */
  luaK_patchtohere(func, expr->f);
  expr->f = NO_JUMP;
}


static void codenot (FuncState *func, Expr *expr) {
  luaK_dischargevars(func, expr);
  switch (expr->kind) {
    case Expr_Nil:
    case Expr_False: {
      expr->kind = Expr_True;
      break;
    }
    case Expr_Constant:
    case Expr_Number:
    case Expr_True: {
      expr->kind = Expr_False;
      break;
    }
    case Expr_Jump: {
      invertjump(func, expr);
      break;
    }
    case Expr_Relocable:
    case Expr_Nonrelocable: {
      discharge2anyreg(func, expr);
      freeexp(func, expr);
      expr->u.s.info = luaK_codeABC(func, OP_NOT, 0, expr->u.s.info, 0);
      expr->kind = Expr_Relocable;
      break;
    }
    default: {
      lua_assert(0);  /* cannot happen */
      break;
    }
  }
  /* interchange true and false lists */
  { int temp = expr->f; expr->f = expr->t; expr->t = temp; }
  removevalues(func, expr->f);
  removevalues(func, expr->t);
}


void luaK_indexed (FuncState *func, Expr *t, Expr *key) {
  t->u.s.aux = luaK_exp2RK(func, key);
  t->kind = Expr_Index;
}


static bool constfolding (OpCode op, Expr *e1, Expr *e2) {
  lua_Number v1, v2, r;
  if (!isnumeral(e1) || !isnumeral(e2))
    return false;
  v1 = e1->u.nval;
  v2 = e2->u.nval;
  switch (op) {
    case OP_ADD: r = luai_numadd(v1, v2); break;
    case OP_SUB: r = luai_numsub(v1, v2); break;
    case OP_MUL: r = luai_nummul(v1, v2); break;
    case OP_DIV:
      if (v2 == 0)
        return false;  /* do not attempt to divide by 0 */
      r = luai_numdiv(v1, v2); break;
    case OP_MOD:
      if (v2 == 0)
        return false;  /* do not attempt to divide by 0 */
      r = luai_nummod(v1, v2); break;
    case OP_POW: r = luai_numpow(v1, v2); break;
    case OP_UNM: r = luai_numunm(v1); break;
    case OP_LEN: return false;  /* no constant folding for 'len' */
    default: lua_assert(0); r = 0; break;
  }
  if (luai_numisnan(r))
    return false;  /* do not attempt to produce NaN */
  e1->u.nval = r;
  return true;
}


static void codearith (FuncState *func, OpCode op, Expr *e1, Expr *e2) {
  if (constfolding(op, e1, e2)) {
    return;
  }
  else {
    int o2 = (op != OP_UNM && op != OP_LEN) ? luaK_exp2RK(func, e2) : 0;
    int o1 = luaK_exp2RK(func, e1);
    if (o1 > o2) {
      freeexp(func, e1);
      freeexp(func, e2);
    }
    else {
      freeexp(func, e2);
      freeexp(func, e1);
    }
    e1->u.s.info = luaK_codeABC(func, op, 0, o1, o2);
    e1->kind = Expr_Relocable;
  }
}


static void codecomp (FuncState *func, OpCode op, bool cond, Expr *e1, Expr *e2) {
  int o1 = luaK_exp2RK(func, e1);
  int o2 = luaK_exp2RK(func, e2);
  freeexp(func, e2);
  freeexp(func, e1);
  if (!cond && op != OP_EQ) {
    int temp;  /* exchange args to replace by `<' or `<=' */
    temp = o1; o1 = o2; o2 = temp;  /* o1 <==> o2 */
    cond = 1;
  }
  e1->u.s.info = condjump(func, op, cond, o1, o2);
  e1->kind = Expr_Jump;
}


void luaK_prefix (FuncState *func, UnOpr op, Expr *expr) {
  Expr e2;
  e2.t = e2.f = NO_JUMP;
  e2.kind = Expr_Number;
  e2.u.nval = 0;

  switch (op) {
    case OPR_MINUS: {
      if (!isnumeral(expr))
        luaK_exp2anyreg(func, expr);  /* cannot operate on non-numeric constants */
      codearith(func, OP_UNM, expr, &e2);
      break;
    }
    case OPR_NOT: codenot(func, expr); break;
    case OPR_LEN: {
      luaK_exp2anyreg(func, expr);  /* cannot operate on constants */
      codearith(func, OP_LEN, expr, &e2);
      break;
    }
    default: lua_assert(0);
  }
}


void luaK_infix (FuncState *func, BinOpr op, Expr *v) {
  switch (op) {
    case OPR_AND: {
      luaK_goiftrue(func, v);
      break;
    }
    case OPR_OR: {
      luaK_goiffalse(func, v);
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2nextreg(func, v);  /* operand must be on the `stack' */
      break;
    }
    case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
    case OPR_MOD: case OPR_POW: {
      if (!isnumeral(v)) /* is not a number literal? */
        luaK_exp2RK(func, v);
      break;
    }
    default: {
      luaK_exp2RK(func, v);
      break;
    }
  }
}


void luaK_posfix (FuncState *func, BinOpr op, Expr *e1, Expr *e2) {
  switch (op) {
    case OPR_AND: {
      lua_assert(e1->t == NO_JUMP);  /* list must be closed */
      luaK_dischargevars(func, e2);
      luaK_concat(func, &e2->f, e1->f);
      *e1 = *e2;
      break;
    }
    case OPR_OR: {
      lua_assert(e1->f == NO_JUMP);  /* list must be closed */
      luaK_dischargevars(func, e2);
      luaK_concat(func, &e2->t, e1->t);
      *e1 = *e2;
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2val(func, e2);
      if (e2->kind == Expr_Relocable && GET_OPCODE(getcode(func, e2)) == OP_CONCAT) {
        lua_assert(e1->u.s.info == GETARG_B(getcode(func, e2))-1);
        freeexp(func, e1);
        SETARG_B(getcode(func, e2), e1->u.s.info);
        e1->kind = Expr_Relocable; e1->u.s.info = e2->u.s.info;
      }
      else {
        luaK_exp2nextreg(func, e2);  /* operand must be on the 'stack' */
        codearith(func, OP_CONCAT, e1, e2);
      }
      break;
    }
    case OPR_ADD: codearith(func, OP_ADD, e1, e2); break;
    case OPR_SUB: codearith(func, OP_SUB, e1, e2); break;
    case OPR_MUL: codearith(func, OP_MUL, e1, e2); break;
    case OPR_DIV: codearith(func, OP_DIV, e1, e2); break;
    case OPR_MOD: codearith(func, OP_MOD, e1, e2); break;
    case OPR_POW: codearith(func, OP_POW, e1, e2); break;
    case OPR_EQ: codecomp(func, OP_EQ, true, e1, e2); break;
    case OPR_NE: codecomp(func, OP_EQ, false, e1, e2); break;
    case OPR_LT: codecomp(func, OP_LT, true, e1, e2); break;
    case OPR_LE: codecomp(func, OP_LE, true, e1, e2); break;
    case OPR_GT: codecomp(func, OP_LT, false, e1, e2); break;
    case OPR_GE: codecomp(func, OP_LE, false, e1, e2); break;
    default: lua_assert(0);
  }
}


void luaK_fixline (FuncState *func, int line) {
  func->proto->lineinfo[func->pc - 1] = line;
}


static int luaK_code (FuncState *func, Instruction i, int line) {
  Proto *f = func->proto;
  dischargejpc(func);  /* `pc' will change */
  /* put new instruction in code array */
  luaM_growvector(func->L, f->code, func->pc, f->size_code, Instruction,
                  MAX_INT, "code size overflow");
  f->code[func->pc] = i;
  /* save corresponding line information */
  luaM_growvector(func->L, f->lineinfo, func->pc, f->size_lineinfo, int,
                  MAX_INT, "code size overflow");
  f->lineinfo[func->pc] = line;
  return func->pc++;
}


int luaK_codeABC (FuncState *func, OpCode o, int a, int b, int c) {
  lua_assert(getOpMode(o) == iABC);
  lua_assert(getBMode(o) != OpArgN || b == 0);
  lua_assert(getCMode(o) != OpArgN || c == 0);
  return luaK_code(func, CREATE_ABC(o, a, b, c), func->lex->lastline);
}


int luaK_codeABx (FuncState *func, OpCode o, int a, unsigned int bc) {
  lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
  lua_assert(getCMode(o) == OpArgN);
  return luaK_code(func, CREATE_ABx(o, a, bc), func->lex->lastline);
}


void luaK_setlist (FuncState *func, int base, int nelems, int tostore) {
  int c =  (nelems - 1)/LFIELDS_PER_FLUSH + 1;
  int b = (tostore == LUA_MULTRET) ? 0 : tostore;
  lua_assert(tostore != 0);
  if (c <= MAXARG_C) {
    luaK_codeABC(func, OP_SETLIST, base, b, c);
  }
  else {
    luaK_codeABC(func, OP_SETLIST, base, b, 0);
    luaK_code(func, cast(Instruction, c), func->lex->lastline);
  }
  func->freereg = base + 1;  /* free registers with list values */
}

