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


static bool hasjumps (Expr *expr) {
  return expr->patch_true != expr->patch_false;
}


static bool isnumeral (Expr *expr) {
  return expr->kind == Expr_Number
      && expr->patch_true == NO_JUMP
      && expr->patch_false == NO_JUMP;
}


static void swap (int *a, int *b) {
  int tmp = *a;
  *a = *b;
  *b = tmp;
}


/**
 * @note 2025-05-11:
 *  Unlike the other functions, this DOES modify `expr->patch_{true,false}`.
 */
static void expr_init_info (Expr *expr, ExprKind kind, int info) {
  expr->kind        = kind;
  expr->u.s.info    = info;
  expr->patch_true  = NO_JUMP;
  expr->patch_false = NO_JUMP;
}


static void expr_set_kind (Expr *expr, ExprKind kind) {
  expr->kind = kind;
}


/**
 * @note 2025-05-11:
 *  - Does NOT set `expr->patch_{true,false}` because they may have values
 *    already.
 */
static void expr_set_info (Expr *expr, ExprKind kind, int info) {
  expr->kind     = kind;
  expr->u.s.info = info;
}


/**
 * @note 2025-05-11:
 *  - Does NOT set `expr->u.s.info`; in the case of `Expr_Index` it already
 *    contains the register of the table we're after.
 */
static void expr_set_aux (Expr *expr, ExprKind kind, int aux) {
  expr->kind    = kind;
  expr->u.s.aux = aux;
}


void luaK_nil (FuncState *fs, int from, int n) {
  int until_reg = from + n - 1;
  if (fs->pc > fs->lasttarget) {  /* no jumps to current position? */
    if (fs->pc == 0) {  /* function start? */
      if (from >= fs->nactvar) {
        return;  /* positions are already clean */
      }
    }
    else {
      Instruction *prev = &fs->proto->code[fs->pc - 1];
      if (GET_OPCODE(*prev) == OP_LOADNIL) {
        int pfrom = GETARG_A(*prev);
        int pto   = GETARG_B(*prev);
        if (pfrom <= from && from <= pto + 1) {  /* can connect both? */
          if (until_reg > pto) {
            SETARG_B(*prev, until_reg);
          }
          return;
        }
      }
    }
  }
  luaK_codeABC(fs, OP_LOADNIL, from, until_reg, 0); /* no optimization */
}


/**
 * @note 2025-05-08
 *  - Concept check: what *is* a jump list? How does it work?
 *
 * @returns
 *  - The pc of `OP_JUMP` we just emitted.
 */
int luaK_jump (FuncState *fs) {
  int jpc = fs->jpc;  /* save list of jumps to here */
  int j;
  fs->jpc = NO_JUMP;

  /* the 'root' jump will always be NO_JUMP to indicate start of list */
  j = luaK_codeAsBx(fs, OP_JMP, 0, NO_JUMP);
  luaK_concat(fs, &j, jpc);  /* keep them on hold */
  return j;
}


void luaK_ret (FuncState *fs, int first, int nret) {
  luaK_codeABC(fs, OP_RETURN, first, nret+1, 0);
}


/**
 * @brief 2025-05-08
 *  Emits `op` with its arguments as normal, but also emits `OP_JUMP`
 *  immediately afterward.
 *
 * @return
 *  The `pc` of the resulting `OP_JUMP`. This is useful to immediately chain
 *  jump lists.
 */
static int condjump (FuncState *fs, OpCode op, int A, int B, int C) {
  luaK_codeABC(fs, op, A, B, C);
  return luaK_jump(fs);
}


static void fixjump (FuncState *fs, int jump_pc, int dest) {
  Instruction *jump_ip = &fs->proto->code[jump_pc];
  int offset = dest - (jump_pc + 1);
  lua_assert(dest != NO_JUMP); /* would be an infinite loop! */
  if (abs(offset) > MAXARG_sBx) {
    luaX_syntaxerror(fs->lexstate, "control structure too long");
  }
  SETARG_sBx(*jump_ip, offset);
}


/*
** returns current `pc' and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
int luaK_getlabel (FuncState *fs) {
  fs->lasttarget = fs->pc;
  return fs->pc;
}


/**
 * @brief
 *  Returns the would-be value of the `sBx` argument in the jump instruction
 *  pointed to by `jump_pc`.
 *
 * @note 2025-05-12:
 *  In the case of jump chains, each jump instruction's `sBx` argument actually
 *  refers to the `pc` of the preceding jump. Think of it like a linked list.
 */
static int getjump (FuncState *fs, int jump_pc) {
  Instruction ip = fs->proto->code[jump_pc];
  int offset = GETARG_sBx(ip);
  if (offset == NO_JUMP) { /* point to itself represents end of jump chain */
    return NO_JUMP;  /* end of jump chain */
  }
  return (jump_pc + 1) + offset;  /* turn offset into absolute position */
}


static Instruction *getjumpcontrol (FuncState *fs, int pc) {
  Instruction *ip = &fs->proto->code[pc];
  /* `ip - 1` is only safe to load and dereference when `pc` greater than 0 */
  if (pc >= 1) {
    Instruction *prev = ip - 1;
    OpCode       op   = GET_OPCODE(*prev);
    /* only comparisons and `OP_TEST(SET)?` are considered tests */
    if (testTMode(op)) {
      return prev;
    }
  }
  return ip;
}


/**
 * @brief
 *  Checks whether at least one jump in the chain starting at `jump_pc` has
 *  a comparison (`OP_(EQ|LT|LE)`) or `OP_TEST`. This indicates that a
 *  particular jump produces a value
 *
 * @note 2025-05-12:
 *  The value 'produced' may be inverted, that is argument A (for comparisons)
 *  is 0 meaning the opposite of the opcode: e.g. `OP_EQ` conceptually becomes
 *  `OP_NEQ`.
 */
static bool need_value (FuncState *fs, int jump_pc) {
  int list_pc = jump_pc;
  while (list_pc != NO_JUMP) {
    int next = getjump(fs, list_pc);
    /* instruction before the jump; jumps in jump chains always have this */
    Instruction ip = *getjumpcontrol(fs, list_pc);
    if (GET_OPCODE(ip) != OP_TESTSET) {
      return true;
    }
    list_pc = next;
  }
  return false;  /* not found */
}


/**
 * @brief
 *  The instruction *before* the jump instruction found at `jump_pc` is termed
 *  the 'control'. It is one of `OP_{EQ,LT,LE,TEST(SET)?}`.
 *
 *  Only `OP_TESTSET` can be patched. In such a case, either its argument A is
 *  updated or it is replaced with `OP_TEST`.
 *
 * @return
 *  `true` if we did patch, else `false` if nothing was done.
 */
static bool patchtestreg (FuncState *fs, int jump_pc, int reg) {
  Instruction *ip = getjumpcontrol(fs, jump_pc);
  int rb, cond;
  if (GET_OPCODE(*ip) != OP_TESTSET) {
    return false;  /* cannot patch other instructions */
  }
  rb   = GETARG_B(*ip);
  cond = GETARG_C(*ip);
  if (reg != NO_REG && reg != rb) {
    SETARG_A(*ip, reg);
  }
  else { /* no register to put value or register already has the value */
    *ip = CREATE_ABC(OP_TEST, rb, 0, cond);
  }
  return true;
}


static void removevalues (FuncState *fs, int list) {
  for (; list != NO_JUMP; list = getjump(fs, list)) {
    patchtestreg(fs, list, NO_REG);
  }
}

/**
 * @brief
 *  Finalizes all the pending jumps in the jump chain starting at `jump_pc`.
 *
 * @todo 2025-05-12:
 *  What does `vtarget` mean?
 */
static void patchlistaux (FuncState *fs, int jump_pc, int vtarget, int reg,
                          int default_target) {
  /* patch all jumps in the chain */
  int list_pc = jump_pc;
  while (list_pc != NO_JUMP) {
    int next = getjump(fs, list_pc);
    if (patchtestreg(fs, list_pc, reg)) {
      fixjump(fs, list_pc, vtarget);
    }
    else {
      fixjump(fs, list_pc, default_target);
    }
    list_pc = next;
  }
}


static void dischargejpc (FuncState *fs) {
  patchlistaux(fs, /* jump_pc: */ fs->jpc, /* vtarget: */ fs->pc,
               /* reg: */ NO_REG, /* default_target: */ fs->pc);
  fs->jpc = NO_JUMP;
}


void luaK_patchlist (FuncState *fs, int list, int target) {
  if (target == fs->pc) {
    luaK_patchtohere(fs, list);
  }
  else {
    lua_assert(target < fs->pc);
    patchlistaux(fs, /* jump_pc: */ list, /* vtarget: */ target,
                /* reg: */ NO_REG, /* default_target: */ target);
  }
}

/* may assign `fs->jpc` */
void luaK_patchtohere (FuncState *fs, int list) {
  luaK_getlabel(fs);
  luaK_concat(fs, &fs->jpc, list); /* set `jpc` if `list != -1` */
}

/**
 * @brief
 *  Get the `pc` of the 'root' jump in the jump chain; that is this is the very
 *  first jump in the chain we emitted.
 *
 * @note 2025-05-12:
 *  Assumption: we have at least one non-`NO_JUMP` in the chain pointed to
 *  by `jump_pc`.
 */
static int getjumproot (FuncState *fs, int jump_pc) {
  int list_pc = jump_pc;
  for (;;) {
    int next_pc = getjump(fs, list_pc);
    /* `list_pc` already contains the `pc` of the root jump itself; assigning
      it to `next_pc` would be disastrous */
    if (next_pc == NO_JUMP) {
      break;
    }
    list_pc = next_pc;
  }
  return list_pc;
}

/**
 * @brief
 *  This function can do one of several things, in order:
 *    1.) `l2` is `NO_JUMP`: Nothing.
 *    2.) `*l1 is NO_JUMP`: Initialize `*l1` with `l2`.
 *    3.) Otherwise: chain `l2` into the root of the jump list from `l1`.
 */
void luaK_concat (FuncState *fs, int *l1, int l2) {
  /* base case #1 (e.g. first jump) */
  if (l2 == NO_JUMP) {
    return;
  }
  /* base case #2 (e.g. assigning `*&expr->f` to `pc` in `luaK_goiftrue()`) */
  else if (*l1 == NO_JUMP) {
    *l1 = l2;
  }
  else {
    fixjump(fs, getjumproot(fs, *l1), l2);
  }
}


void luaK_checkstack (FuncState *fs, int n) {
  int newstack = fs->freereg + n;
  if (newstack > fs->proto->maxstacksize) {
    if (newstack >= MAXSTACK)
      luaX_syntaxerror(fs->lexstate, "function or expression too complex");
    fs->proto->maxstacksize = cast_byte(newstack);
  }
}


void luaK_reserveregs (FuncState *fs, int n) {
  luaK_checkstack(fs, n);
  fs->freereg += n;
}


static void freereg (FuncState *fs, int reg) {
  if (!ISK(reg) && reg >= fs->nactvar) {
    fs->freereg--;
    lua_assert(reg == fs->freereg);
  }
}


static void freeexp (FuncState *fs, const Expr *expr) {
  if (expr->kind == Expr_Nonrelocable)
    freereg(fs, expr->u.s.info);
}


static int addk (FuncState *fs, TValue *k, TValue *v) {
  lua_State *L = fs->L;
  TValue *idx = luaH_set(L, fs->h, k);
  Proto *proto = fs->proto;
  int oldsize = proto->size_constants;
  if (ttisnumber(idx)) {
    /**
     * @note 2025-04-07:
     *  Originally contained `&fs->f->k` which is equivalent to our new
     *  `&fs->proto->constants`.
     */
    lua_assert(luaO_rawequalObj(&proto->constants[cast_int(nvalue(idx))], v));
    return cast_int(nvalue(idx));
  }
  else {  /* constant not found; create a new entry */
    setnvalue(idx, cast_num(fs->nconstants));
    luaM_growvector(L, proto->constants, fs->nconstants, proto->size_constants,
                    TValue, MAXARG_Bx, "constant table overflow");
    while (oldsize < proto->size_constants) {
      setnilvalue(&proto->constants[oldsize++]);
    }
    setobj(L, &proto->constants[fs->nconstants], v);
    luaC_barrier(L, proto, v);
    return fs->nconstants++;
  }
}


int luaK_stringK (FuncState *fs, TString *s) {
  TValue o;
  setsvalue(fs->L, &o, s);
  return addk(fs, &o, &o);
}


int luaK_numberK (FuncState *fs, lua_Number r) {
  TValue o;
  setnvalue(&o, r);
  return addk(fs, &o, &o);
}


static int boolK (FuncState *fs, int b) {
  TValue o;
  setbvalue(&o, b);
  return addk(fs, &o, &o);
}


static int nilK (FuncState *fs) {
  TValue k, v;
  setnilvalue(&v);
  /* cannot use nil as key; instead use table itself to represent nil */
  sethvalue(fs->L, &k, fs->h);
  return addk(fs, &k, &v);
}


void luaK_setreturns (FuncState *fs, Expr *expr, int nresults) {
  Instruction *ip;
  if (expr->kind == Expr_Call) {  /* expression is an open function call? */
    ip = getcode(fs, expr);
    SETARG_C(*ip, nresults+1);
  }
  else if (expr->kind == Expr_Vararg) {
    ip = getcode(fs, expr);
    SETARG_B(*ip, nresults+1);
    SETARG_A(*ip, fs->freereg);
    luaK_reserveregs(fs, 1);
  }
}


void luaK_setoneret (FuncState *fs, Expr *expr) {
  Instruction *ip;
  if (expr->kind == Expr_Call) {  /* expression is an open function call? */
    ip = getcode(fs, expr);
    expr_set_info(expr, Expr_Nonrelocable, GETARG_A(*ip));
  }
  else if (expr->kind == Expr_Vararg) {
    ip = getcode(fs, expr);
    SETARG_B(*ip, 2);
    expr_set_kind(expr, Expr_Relocable); /* can relocate its simple result */
  }
}


void luaK_dischargevars (FuncState *fs, Expr *expr) {
  switch (expr->kind) {
    case Expr_Local: { /* info is already a local register */
      expr_set_kind(expr, Expr_Nonrelocable);
      break;
    }
    case Expr_Upvalue: {
      int pc = luaK_codeABC(fs, OP_GETUPVAL, 0, expr->u.s.info, 0);
      expr_set_info(expr, Expr_Relocable, pc);
      break;
    }
    case Expr_Global: {
      int pc = luaK_codeABx(fs, OP_GETGLOBAL, 0, expr->u.s.info);
      expr_set_info(expr, Expr_Relocable, pc);
      break;
    }
    case Expr_Index: {
      int pc;
      int table_reg = expr->u.s.info;
      int key_reg   = expr->u.s.aux;
      freereg(fs, key_reg); /* reuse these registers, popped in order */
      freereg(fs, table_reg);
      pc = luaK_codeABC(fs, OP_GETTABLE, 0, table_reg, key_reg);
      expr_set_info(expr, Expr_Relocable, pc);
      break;
    }
    case Expr_Vararg:
    case Expr_Call: {
      luaK_setoneret(fs, expr);
      break;
    }
    default: break;  /* there is one value available (somewhere) */
  }
}


static int code_label (FuncState *fs, int A, bool b, bool jump) {
  luaK_getlabel(fs);  /* those instructions may be jump targets */
  return luaK_codeABC(fs, OP_LOADBOOL, A, cast_int(b), cast_int(jump));
}


/**
 * @brief
 *  Uncondtionally emits into `reg` the retrieval of variables (globals,
 *  locals, table fields and upvalues), as well as literals and constants.
 *
 *  `reg` is transformed into type `Expr_Nonrelocable`.
 */
static void discharge2reg (FuncState *fs, Expr *expr, int reg) {
  luaK_dischargevars(fs, expr);
  switch (expr->kind) {
    case Expr_Nil: {
      luaK_nil(fs, reg, 1);
      break;
    }
    case Expr_False:  case Expr_True: {
      luaK_codeABC(fs, OP_LOADBOOL, reg, expr->kind == Expr_True, 0);
      break;
    }
    case Expr_Constant: {
      luaK_codeABx(fs, OP_LOADK, reg, expr->u.s.info);
      break;
    }
    case Expr_Number: {
      luaK_codeABx(fs, OP_LOADK, reg, luaK_numberK(fs, expr->u.nval));
      break;
    }
    case Expr_Relocable: { /* global, upvalue, table field */
      Instruction *pc = getcode(fs, expr);
      SETARG_A(*pc, reg);
      break;
    }
    case Expr_Nonrelocable: { /* local or temporary */
      /* destination is NOT just the local/temporary itself? If so, this is
        most likely a get operation. */
      if (reg != expr->u.s.info) {
        luaK_codeABC(fs, OP_MOVE, reg, expr->u.s.info, 0);
      }
      break;
    }
    default: {
      lua_assert(expr->kind == Expr_Void || expr->kind == Expr_Jump);
      return;  /* nothing to do... */
    }
  }
  expr_set_info(expr, Expr_Nonrelocable, reg);
}


/**
 * @brief
 *  Push `expr` to the top of the stack if it's not already of type
 *  `Expr_Nonrelocable`. This guarantees `expr` will have a register.
 */
static void discharge2anyreg (FuncState *fs, Expr *expr) {
  if (expr->kind != Expr_Nonrelocable) {
    luaK_reserveregs(fs, 1);
    discharge2reg(fs, expr, fs->freereg - 1);
  }
}


/**
 * @brief
 *  Emits `expr` to `reg` no matter what.
 *
 * @note 2025-05-11:
 *  This is the main workhorse when it comes to register emission.
 *  All the `luaK_exp2*` functions eventually delegate to this one.
 */
static void exp2reg (FuncState *fs, Expr *expr, int reg) {
  discharge2reg(fs, expr, reg);
  if (expr->kind == Expr_Jump) {
    /* expr_set (somewhat) ; always updates `patch_true` */
    luaK_concat(fs, &expr->patch_true, expr->u.s.info);  /* put this jump in `t' list */
  }
  if (hasjumps(expr)) {
    int final;  /* position after whole expression */
    int p_f = NO_JUMP;  /* position of an eventual LOAD false */
    int p_t = NO_JUMP;  /* position of an eventual LOAD true */
    if (need_value(fs, expr->patch_true)
      || need_value(fs, expr->patch_false)) {
      int fj = (expr->kind == Expr_Jump) ? NO_JUMP : luaK_jump(fs);
      p_f = code_label(fs, reg, /* b: */ false, /* cond: */ true);
      p_t = code_label(fs, reg, /* b: */ true,  /* cond: */ false);
      luaK_patchtohere(fs, fj);
    }
    final = luaK_getlabel(fs);
    patchlistaux(fs, /* jump_pc: */ expr->patch_false, /* vtarget: */ final,
                /* reg: */ reg, /* default_target: */ p_f);

    patchlistaux(fs, /* jump_pc: */ expr->patch_true, /* vtarget: */ final,
                /* reg: */ reg, /* default_target: */ p_t);
  }
  expr_init_info(expr, Expr_Nonrelocable, reg);
}


void luaK_exp2nextreg (FuncState *fs, Expr *expr) {
  luaK_dischargevars(fs, expr);
  freeexp(fs, expr);
  luaK_reserveregs(fs, 1);
  exp2reg(fs, expr, fs->freereg - 1); /* freereg - 1 is the register we want */
}


int luaK_exp2anyreg (FuncState *fs, Expr *expr) {
  luaK_dischargevars(fs, expr);
  if (expr->kind == Expr_Nonrelocable) {
    if (!hasjumps(expr)) {
      return expr->u.s.info;  /* exp is already in a register */
    }

    if (expr->u.s.info >= fs->nactvar) {  /* reg. is not a local? */
      exp2reg(fs, expr, expr->u.s.info);  /* put value on it */
      return expr->u.s.info;
    }
  }
  luaK_exp2nextreg(fs, expr);  /* default */
  return expr->u.s.info;
}


void luaK_exp2val (FuncState *fs, Expr *expr) {
  if (hasjumps(expr)) {
    luaK_exp2anyreg(fs, expr);
  }
  else {
    luaK_dischargevars(fs, expr);
  }
}


static int addliteral (FuncState *fs, const Expr *expr) {
  switch (expr->kind) {
  case Expr_Nil:    return nilK(fs);
  case Expr_Number: return luaK_numberK(fs, expr->u.nval);
  default:          return boolK(fs, expr->kind == Expr_True);
  }
}

int luaK_exp2RK (FuncState *fs, Expr *expr) {
  luaK_exp2val(fs, expr);
  switch (expr->kind) {
    case Expr_Number:
    case Expr_True:
    case Expr_False:
    case Expr_Nil: {
      if (fs->nconstants <= MAXINDEXRK) {  /* constant fit in RK operand? */
        int info = addliteral(fs, expr);
        expr_set_info(expr, Expr_Constant, info);
        return RKASK(info);
      }
      else break;
    }
    case Expr_Constant: {
      if (expr->u.s.info <= MAXINDEXRK) {/* constant fit in argC? */
        return RKASK(expr->u.s.info);
      }
      else { /* doesn't fit in C; proceed to base case to push it */
        break;
      }
    }
    default: break;
  }
  /* not a constant in the right range: put it in a register */
  return luaK_exp2anyreg(fs, expr);
}


/**
 * @brief
 *  Emits the bytecode to set global, local, table, or upvalue variables.
 */
void luaK_storevar (FuncState *fs, Expr *var, Expr *expr) {
  switch (var->kind) {
    case Expr_Local: {
      int local = var->u.s.info;
      freeexp(fs, expr); /* free it if it's a temporary register */
      exp2reg(fs, expr, local); /* reuse local register or code OP_MOVE */
      return;
    }
    case Expr_Upvalue: {
      /**
       * @note 2025-04-07:
       *  Originally named `expr`, confusingly enough!
       */
      int reg = luaK_exp2anyreg(fs, expr);
      luaK_codeABC(fs, OP_SETUPVAL, reg, var->u.s.info, 0);
      break;
    }
    case Expr_Global: {
      int reg   = luaK_exp2anyreg(fs, expr);
      int index = var->u.s.info;
      luaK_codeABx(fs, OP_SETGLOBAL, reg, index);
      break;
    }
    case Expr_Index: {
      int table = var->u.s.info;
      int key   = var->u.s.aux;
      int rkc   = luaK_exp2RK(fs, expr);
      luaK_codeABC(fs, OP_SETTABLE, table, key, rkc);
      break;
    }
    default: {
      lua_assert(0);  /* invalid var kind to store */
      break;
    }
  }
  freeexp(fs, expr);
}


void luaK_self (FuncState *fs, Expr *expr, Expr *key) {
  /**
   * @brief
   *  Contains the register of the `self` parameter.
   *
   * @note 2025-04-07:
   *  Originally called `func`, confusingly enough!
   */
  int reg;
  luaK_exp2anyreg(fs, expr);
  freeexp(fs, expr);
  reg = fs->freereg;
  luaK_reserveregs(fs, 2); /* reserve `self` and key */
  luaK_codeABC(fs, OP_SELF, reg, expr->u.s.info, luaK_exp2RK(fs, key));
  freeexp(fs, key); /* `key`'s register no longer needed; can be reused */

  expr_set_info(expr, Expr_Nonrelocable, reg);
}


/**
 * @note 2025-05-11:
 *  - Mainly applies only to comparison instructions. Hence `OP_TEST(SET)?`
 *    should not reach this point because how they use argument A is very
 *    different than the comparisons.
 */
static void invertjump (FuncState *fs, Expr *expr) {
  Instruction *pc = getjumpcontrol(fs, expr->u.s.info);
  lua_assert(testTMode(GET_OPCODE(*pc))
          && GET_OPCODE(*pc) != OP_TESTSET
          && GET_OPCODE(*pc) != OP_TEST);

  /* comparisons use argument A to check if the result needs to be flipped */
  SETARG_A(*pc, !(GETARG_A(*pc)));
}


/**
 * @brief
 *  Create an `OP_TEST` and `OP_JMP` pair. If the test does not pass,
 *  then that means the jump isn't skipped.
 *
 * @param cond
 *  The boolean condition which the test must meet in order to not skip the
 *  jump, thus actually performing the jump.
 *
 * @returns
 *  The index of the `OP_JMP` instruction we just created.
 */
static int jumponcond (FuncState *fs, Expr *expr, bool cond) {
  if (expr->kind == Expr_Relocable) {
    Instruction ip = *getcode(fs, expr);
    if (GET_OPCODE(ip) == OP_NOT) {
      fs->pc--;  /* remove previous OP_NOT */
      return condjump(fs, OP_TEST, GETARG_B(ip), 0, cast_int(!cond));
    }
    /* else go through */
  }
  discharge2anyreg(fs, expr);
  freeexp(fs, expr);
  return condjump(fs, OP_TESTSET, NO_REG, expr->u.s.info, cast_int(cond));
}


/**
 * @details 2025-05-08: Sample callstack
 *  - lparser.c:statement(LexState *ls)
 *  - lparser.c:if_stmt(LexState *ls, int line)
 *  - lparser.c:test_then_block(LexState *ls)
 *  - lparser.c:cond(LexState *ls)
 *  - lcode.c:luaK_goiftrue(FuncState *fs, expdesc *e = cond:expr)
 */
void luaK_goiftrue (FuncState *fs, Expr *expr) {
  int pc;  /* pc of last jump */
  luaK_dischargevars(fs, expr);
  switch (expr->kind) {
    case Expr_Constant:
    case Expr_Number:
    case Expr_True: {
      /**
       * @note 2025-05-08
       *  Always true hence we do nothing here.
       *
       * @note 2025-05-12:
       *  Even though `luaK_exp2rk()` can add `false` and `nil` to the
       *  constants array, it is only called in the middle of binary arithmetic
       *  logical/expressions.
       *
       *  So for conditions, it is safe to assume we never use falsy values
       *  from the constants array.
       */
      pc = NO_JUMP;
      break;
    }
    case Expr_Jump: {
      invertjump(fs, expr);
      pc = expr->u.s.info;
      break;
    }
    default: {
      /**
       * @brief
       *  `expr` is potentially falsy; don't skip jump when condition returns
       *  false.
       *
       * @note
       *  Even if we have literal `false`, we can't assume we can optimize it
       *  out because it may be an `elseif` block which DOES need a jump no
       *  matter what.
       */
      pc = jumponcond(fs, expr, false);
      break;
    }
  }
  /**
   * @brief 2025-05-10:
   *  inserts the last jump in `f` list
   *
   * @note 2025-05-10:
   *  For the first instance of a conditional, e.g. `if x then end`, `expr.f`
   *  is `NO_JUMP` and `pc >= 0` so we will almost always set `expr.f`.
   */
  luaK_concat(fs, &expr->patch_false, pc);
  luaK_patchtohere(fs, expr->patch_true);
  /* expr_set (somewhat) */
  expr->patch_true = NO_JUMP;
}


static void luaK_goiffalse (FuncState *fs, Expr *expr) {
  int pc;  /* pc of last jump */
  luaK_dischargevars(fs, expr);
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
      pc = jumponcond(fs, expr, true);
      break;
    }
  }
  luaK_concat(fs, &expr->patch_true, pc);  /* insert last jump in `t' list */
  luaK_patchtohere(fs, expr->patch_false);
  /* expr_set (somewhat) */
  expr->patch_false = NO_JUMP;
}


static void codenot (FuncState *fs, Expr *expr) {
  luaK_dischargevars(fs, expr);
  switch (expr->kind) {
    case Expr_Nil:
    case Expr_False: {
      expr_set_kind(expr, Expr_True);
      break;
    }
    case Expr_Constant:
    case Expr_Number:
    case Expr_True: {
      expr_set_kind(expr, Expr_False);
      break;
    }
    case Expr_Jump: {
      /* only occurs for comparisons; just flip argument A */
      invertjump(fs, expr);
      break;
    }
    case Expr_Relocable:
    case Expr_Nonrelocable: {
      int pc;
      discharge2anyreg(fs, expr);
      freeexp(fs, expr);
      pc = luaK_codeABC(fs, OP_NOT, 0, expr->u.s.info, 0);
      expr_set_info(expr, Expr_Relocable, pc);
      break;
    }
    default: {
      lua_assert(0);  /* cannot happen */
      break;
    }
  }
  /* interchange true and false lists */
  swap(&expr->patch_false, &expr->patch_true);
  removevalues(fs, expr->patch_false);
  removevalues(fs, expr->patch_true);
}


void luaK_indexed (FuncState *fs, Expr *table, Expr *key) {
  int key_reg = luaK_exp2RK(fs, key);
  expr_set_aux(table, Expr_Index, key_reg);
}


static bool constfolding (OpCode op, Expr *left, Expr *right) {
  lua_Number v1, v2, r;
  if (!isnumeral(left) || !isnumeral(right))
    return false;
  v1 = left->u.nval;
  v2 = right->u.nval;
  switch (op) {
    case OP_ADD: r = luai_numadd(v1, v2); break;
    case OP_SUB: r = luai_numsub(v1, v2); break;
    case OP_MUL: r = luai_nummul(v1, v2); break;
    case OP_DIV:
      if (v2 == 0) {
        return false;  /* do not attempt to divide by 0 */
      }
      r = luai_numdiv(v1, v2); break;
    case OP_MOD:
      if (v2 == 0) {
        return false;  /* do not attempt to divide by 0 */
      }
      r = luai_nummod(v1, v2); break;
    case OP_POW: r = luai_numpow(v1, v2); break;
    case OP_UNM: r = luai_numunm(v1); break;
    case OP_LEN: return false;  /* no constant folding for 'len' */
    default: lua_assert(0); r = 0; break;
  }
  if (luai_numisnan(r)) {
    return false;  /* do not attempt to produce NaN */
  }

  /* expr_set (somewhat) ; assuming `left` is already a number */
  left->u.nval = r;
  return true;
}


static void codearith (FuncState *fs, OpCode op, Expr *left, Expr *right) {
  if (constfolding(op, left, right)) {
    return;
  }
  else {
    int pc;
    /* unused for unary; assumes `right` is a dummy `.Number` */
    int rkc = (op != OP_UNM && op != OP_LEN) ? luaK_exp2RK(fs, right) : 0;
    int rkb = luaK_exp2RK(fs, left);
    if (rkb > rkc) { /* pop used registers in correct order */
      freeexp(fs, left);
      freeexp(fs, right);
    }
    else {
      freeexp(fs, right);
      freeexp(fs, left);
    }

    pc = luaK_codeABC(fs, op, 0, rkb, rkc);
    expr_set_info(left, Expr_Relocable, pc);
  }
}


/**
 * @brief
 *  Transforms `left` into `Expr_Jump` where its `info` is the pc of the
 *  comparison instruction.
 */
static void codecomp (FuncState *fs, OpCode op, bool cond, Expr *left,
                      Expr *right) {
  int pc;
  int rkb = luaK_exp2RK(fs, left);
  int rkc = luaK_exp2RK(fs, right);
  freeexp(fs, right);
  freeexp(fs, left);
  if (!cond && op != OP_EQ) {
    /* exchange args to replace by `<' or `<='; rkb <==> rkc */
    swap(&rkb, &rkc);
    cond = true;
  }

  pc = condjump(fs, op, cond, rkb, rkc);
  expr_set_info(left, Expr_Jump, pc);
}


void luaK_prefix (FuncState *fs, UnOpr op, Expr *left) {
  Expr dummy; /* needed so `constfolding` never receives a `NULL` argument */
  expr_init_info(&dummy, Expr_Number, 0);
  dummy.u.nval = 0;

  switch (op) {
    case OPR_MINUS: {
      if (!isnumeral(left)) {
        luaK_exp2anyreg(fs, left); /* can't fold non-numeric constants */
      }
      codearith(fs, OP_UNM, left, &dummy);
      break;
    }
    case OPR_NOT: codenot(fs, left); break;
    case OPR_LEN: {
      luaK_exp2anyreg(fs, left); /* can never operate on constants */
      codearith(fs, OP_LEN, left, &dummy);
      break;
    }
    default: lua_assert(0);
  }
}


void luaK_infix (FuncState *fs, BinOpr op, Expr *left) {
  switch (op) {
    case OPR_AND: {
      luaK_goiftrue(fs, left);
      break;
    }
    case OPR_OR: {
      luaK_goiffalse(fs, left);
      break;
    }
    case OPR_CONCAT: {
      luaK_exp2nextreg(fs, left);  /* operand must be on the `stack' */
      break;
    }
    case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
    case OPR_MOD: case OPR_POW: {
      if (!isnumeral(left)) /* is not a number literal? */
        luaK_exp2RK(fs, left);
      break;
    }
    default: {
      luaK_exp2RK(fs, left);
      break;
    }
  }
}


void luaK_posfix (FuncState *fs, BinOpr op, Expr *left, Expr *right) {
  switch (op) {
    case OPR_AND: {
      lua_assert(left->patch_true == NO_JUMP);  /* list must be closed */
      luaK_dischargevars(fs, right);
      luaK_concat(fs, &right->patch_false, left->patch_false);
      /* expr_set */
      *left = *right;
      break;
    }
    case OPR_OR: {
      lua_assert(left->patch_false == NO_JUMP);  /* list must be closed */
      luaK_dischargevars(fs, right);
      luaK_concat(fs, &right->patch_true, left->patch_true);
      /* expr_set */
      *left = *right;
      break;
    }
    case OPR_CONCAT: {
      Instruction *ip;
      luaK_exp2val(fs, right);
      ip = getcode(fs, right);
      if (right->kind == Expr_Relocable && GET_OPCODE(*ip) == OP_CONCAT) {
        int left_reg = left->u.s.info;
        lua_assert(left_reg == GETARG_B(*ip) - 1);
        freeexp(fs, left);
        SETARG_B(*ip, left_reg);
        expr_set_info(left, Expr_Relocable, right->u.s.info);
      }
      else {
        luaK_exp2nextreg(fs, right);  /* operand must be on the 'stack' */
        codearith(fs, OP_CONCAT, left, right);
      }
      break;
    }
    case OPR_ADD: codearith(fs, OP_ADD, left, right); break;
    case OPR_SUB: codearith(fs, OP_SUB, left, right); break;
    case OPR_MUL: codearith(fs, OP_MUL, left, right); break;
    case OPR_DIV: codearith(fs, OP_DIV, left, right); break;
    case OPR_MOD: codearith(fs, OP_MOD, left, right); break;
    case OPR_POW: codearith(fs, OP_POW, left, right); break;
    case OPR_EQ:  codecomp(fs,  OP_EQ,  true,  left, right); break;
    case OPR_NE:  codecomp(fs,  OP_EQ,  false, left, right); break;
    case OPR_LT:  codecomp(fs,  OP_LT,  true,  left, right); break;
    case OPR_LE:  codecomp(fs,  OP_LE,  true,  left, right); break;
    case OPR_GT:  codecomp(fs,  OP_LT,  false, left, right); break;
    case OPR_GE:  codecomp(fs,  OP_LE,  false, left, right); break;
    default: lua_assert(0);
  }
}


void luaK_fixline (FuncState *fs, int line) {
  fs->proto->lineinfo[fs->pc - 1] = line;
}


static int luaK_code (FuncState *fs, Instruction i, int line) {
  Proto *proto = fs->proto;
  dischargejpc(fs);  /* `pc' will change */

  /* put new instruction in code array */
  luaM_growvector(fs->L, proto->code, fs->pc, proto->size_code, Instruction,
                  MAX_INT, "code size overflow");
  proto->code[fs->pc] = i;

  /* save corresponding line information */
  luaM_growvector(fs->L, proto->lineinfo, fs->pc, proto->size_lineinfo, int,
                  MAX_INT, "code size overflow");
  proto->lineinfo[fs->pc] = line;

  return fs->pc++;
}


int luaK_codeABC (FuncState *fs, OpCode o, int a, int b, int c) {
  lua_assert(getOpMode(o) == iABC);
  lua_assert(getBMode(o) != OpArgN || b == 0);
  lua_assert(getCMode(o) != OpArgN || c == 0);
  return luaK_code(fs, CREATE_ABC(o, a, b, c), fs->lexstate->lastline);
}


int luaK_codeABx (FuncState *fs, OpCode o, int a, unsigned int bc) {
  lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
  lua_assert(getCMode(o) == OpArgN);
  return luaK_code(fs, CREATE_ABx(o, a, bc), fs->lexstate->lastline);
}


void luaK_setlist (FuncState *fs, int base, int nelems, int tostore) {
  int c = (nelems - 1)/LFIELDS_PER_FLUSH + 1;
  int b = (tostore == LUA_MULTRET) ? 0 : tostore;
  lua_assert(tostore != 0);
  if (c <= MAXARG_C) {
    luaK_codeABC(fs, OP_SETLIST, base, b, c);
  }
  else {
    /* not in range, use next "instruction" to encode pending count as-is */
    luaK_codeABC(fs, OP_SETLIST, base, b, 0);
    luaK_code(fs, cast(Instruction, c), fs->lexstate->lastline);
  }
  fs->freereg = base + 1;  /* free registers with list values */
}

