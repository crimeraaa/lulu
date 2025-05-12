/*
** $Id: lparser.c,v 2.42.1.4 2011/10/21 19:31:42 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lparser_c
#define LUA_CORE

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"



#define hasmultret(k)		((k) == Expr_Call || (k) == Expr_Vararg)

#define getlocvar(func, i)	((func)->proto->locvars[(func)->actvar[i]])

#define luaY_checklimit(func, value, limit, what)	\
  if ((value) > (limit)) error_limit(func, limit, what)


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* chain */
  int breaklist;  /* list of jumps out of this loop */
  lu_byte nactvar;  /* # active locals outside the breakable structure */
  bool upval;  /* true if some variable in the block is an upvalue */
  lu_byte isbreakable;  /* true if `block' is a loop */
} BlockCnt;



/*
** prototypes for recursive non-terminal functions
*/
static void chunk (LexState *lex);
static void expression (LexState *lex, Expr *var);


static void anchor_token (LexState *lex) {
  if (lex->current.type == Token_Name || lex->current.type == Token_String) {
    TString *ts = lex->current.seminfo.ts;
    luaX_newstring(lex, getstr(ts), ts->tsv.len);
  }
}


static void error_expected (LexState *lex, TokenType type) {
  const char *token = luaX_token2str(lex, type);
  luaX_syntaxerror(lex, luaO_pushfstring(lex->L, LUA_QS " expected", token));
}


static void error_limit (FuncState *fs, int limit, const char *what) {
  const char *msg = (fs->proto->linedefined == 0)
    ? luaO_pushfstring(fs->L, "main function has more than %d %s", limit, what)
    : luaO_pushfstring(fs->L, "function at line %d has more than %d %s",
                       fs->proto->linedefined, limit, what);
  luaX_lexerror(fs->lexstate, msg, Token_Error);
}


/**
 * @brief
 *  If the current token matches `expected`, advances and returns `true`.
 *  Otherwise does nothing and returns `false`.
 */
static bool test_next (LexState *lex, TokenType expected) {
  if (lex->current.type == expected) {
    luaX_next(lex);
    return true;
  }
  else {
    return false;
  }
}


/**
 * @brief
 *  Asserts that the current token matches `expected`.
 */
static void check (LexState *lex, TokenType expected) {
  if (lex->current.type != expected)
    error_expected(lex, expected);
}

/**
 * @brief
 *   Asserts that current token matches `expected` then advances.
 */
static void check_next (LexState *lex, TokenType expected) {
  check(lex, expected);
  luaX_next(lex);
}


#define check_condition(ls, c, msg)	{ if (!(c)) luaX_syntaxerror(ls, msg); }



static void check_match (LexState *lex, TokenType what, TokenType who, int where) {
  if (!test_next(lex, what)) {
    if (where == lex->linenumber)
      error_expected(lex, what);
    else {
      luaX_syntaxerror(lex, luaO_pushfstring(lex->L,
             LUA_QS " expected (to close " LUA_QS " at line %d)",
              luaX_token2str(lex, what), luaX_token2str(lex, who), where));
    }
  }
}


static TString *str_checkname (LexState *lex) {
  TString *ts;
  check(lex, Token_Name);
  ts = lex->current.seminfo.ts;
  luaX_next(lex);
  return ts;
}


static void init_exp (Expr *expr, ExprKind kind, int info) {
  expr->patch_false = NO_JUMP;
  expr->patch_true  = NO_JUMP;
  expr->kind        = kind;
  expr->u.s.info    = info;
}


static void codestring (LexState *lex, Expr *expr, TString *s) {
  init_exp(expr, Expr_Constant, luaK_stringK(lex->funcstate, s));
}


static void checkname (LexState *lex, Expr *expr) {
  codestring(lex, expr, str_checkname(lex));
}


static int registerlocalvar (LexState *lex, TString *varname) {
  FuncState *fs = lex->funcstate;
  Proto *proto = fs->proto;
  int oldsize = proto->size_locvars;
  luaM_growvector(lex->L, proto->locvars, fs->nlocvars, proto->size_locvars,
    LocVar, SHRT_MAX, "too many local variables");

  while (oldsize < proto->size_locvars) /* initialize new region */
    proto->locvars[oldsize++].varname = NULL;

  /* declare first available local variable */
  proto->locvars[fs->nlocvars].varname = varname;
  luaC_objbarrier(lex->L, proto, varname);
  return fs->nlocvars++;
}


#define new_localvarliteral(ls,v,n) \
  new_localvar(lex, luaX_newstring(lex, "" v, (sizeof(v)/sizeof(char))-1), n)


static void new_localvar (LexState *lex, TString *name, int n) {
  FuncState *fs = lex->funcstate;
  int locvar;
  luaY_checklimit(fs, fs->nactvar+n+1, LUAI_MAXVARS, "local variables");

  /* each active variable is merely a generational index into
    `func->proto->locvars[]` */
  locvar = registerlocalvar(lex, name);
  fs->actvar[fs->nactvar + n] = cast(unsigned short, locvar);
}


static void adjustlocalvars (LexState *lex, int nvars) {
  FuncState *fs = lex->funcstate;
  int startpc = fs->pc;
  fs->nactvar += cast_byte(nvars);
  for (; nvars; nvars--) {
    LocVar *loc = &getlocvar(fs, cast_int(fs->nactvar) - nvars);
    loc->startpc = startpc;
  }
}


static void removevars (LexState *lex, int tolevel) {
  FuncState *fs = lex->funcstate;
  int endpc = fs->pc;
  while (fs->nactvar > tolevel) {
    LocVar *loc = &getlocvar(fs, --fs->nactvar);
    loc->endpc = endpc;
  }
}


static int indexupvalue (FuncState *fs, TString *name, Expr *var) {
  int i;
  Proto *proto = fs->proto;
  int oldsize = proto->size_upvalues;
  for (i=0; i<proto->nups; i++) {
    if (fs->upvalues[i].k == var->kind
      && fs->upvalues[i].info == var->u.s.info) {
      lua_assert(proto->upvalues[i] == name);
      return i;
    }
  }
  /* new one */
  luaY_checklimit(fs, proto->nups + 1, LUAI_MAXUPVALUES, "upvalues");
  luaM_growvector(fs->L, proto->upvalues, proto->nups, proto->size_upvalues,
    TString *, MAX_INT, "");

  while (oldsize < proto->size_upvalues) {
    proto->upvalues[oldsize++] = NULL;
  }
  proto->upvalues[proto->nups] = name;
  luaC_objbarrier(fs->L, proto, name);
  lua_assert(var->kind == Expr_Local || var->kind == Expr_Upvalue);
  fs->upvalues[proto->nups].k = cast_byte(var->kind);
  fs->upvalues[proto->nups].info = cast_byte(var->u.s.info);
  return proto->nups++;
}


static int searchvar (FuncState *fs, TString *n) {
  int i;
  for (i = fs->nactvar - 1; i >= 0; i--) {
    if (n == getlocvar(fs, i).varname) {
      return i;
    }
  }
  return -1;  /* not found */
}


static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl && bl->nactvar > level) {
    bl = bl->previous;
  }
  if (bl) {
    bl->upval = true;
  }
}


static int singlevaraux (FuncState *fs, TString *n, Expr *var, bool base) {
  if (fs == NULL) {  /* no more levels? */
    init_exp(var, Expr_Global, NO_REG);  /* default is global variable */
    return Expr_Global;
  }
  else {
    int v = searchvar(fs, n);  /* look up at current level */
    if (v >= 0) {
      init_exp(var, Expr_Local, v);
      if (!base) {
        markupval(fs, v);  /* local will be used as an upval */
      }
      return Expr_Local;
    }
    else {  /* not found at current level; try upper one */
      if (singlevaraux(fs->prev, n, var, false) == Expr_Global) {
        return Expr_Global;
      }

      /* init_exp-like but does not set the patch lists */
      var->u.s.info = indexupvalue(fs, n, var);  /* else was LOCAL or UPVAL */
      var->kind = Expr_Upvalue;  /* upvalue in this level */
      return Expr_Upvalue;
    }
  }
}


static void singlevar (LexState *lex, Expr *var) {
  TString *varname = str_checkname(lex);
  FuncState *fs = lex->funcstate;
  if (singlevaraux(fs, varname, var, true) == Expr_Global) {
    /* init_exp */
    var->u.s.info = luaK_stringK(fs, varname);  /* info points to global name */
  }
}


static void adjust_assign (LexState *lex, int nvars, int nexps, Expr *expr) {
  FuncState *fs = lex->funcstate;
  int extra = nvars - nexps;
  if (hasmultret(expr->kind)) {
    extra++;  /* includes call itself */
    if (extra < 0) {
      extra = 0;
    }
    luaK_setreturns(fs, expr, extra);  /* last exp. provides the difference */
    if (extra > 1) {
      luaK_reserveregs(fs, extra-1);
    }
  }
  else {
    if (expr->kind != Expr_Void) {
      luaK_exp2nextreg(fs, expr);  /* close last expression */
    }
    if (extra > 0) {
      int reg = fs->freereg; /* reg of first uninitialized left-hand-side */
      luaK_reserveregs(fs, extra);
      luaK_nil(fs, reg, extra);
    }
  }
}


static void enterlevel (LexState *lex) {
  if (++lex->L->nCcalls > LUAI_MAXCCALLS)
    luaX_lexerror(lex, "chunk has too many syntax levels", Token_Error);
}


#define leavelevel(ls)	((ls)->L->nCcalls--)


static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isbreakable) {
  bl->breaklist = NO_JUMP;
  bl->isbreakable = isbreakable;
  bl->nactvar = fs->nactvar;
  bl->upval = false;
  bl->previous = fs->bl;
  fs->bl = bl;
  lua_assert(fs->freereg == fs->nactvar);
}


static void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  fs->bl = bl->previous;
  removevars(fs->lexstate, bl->nactvar);
  if (bl->upval) {
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
  }
  /* a block either controls scope or breaks (never both) */
  lua_assert(!bl->isbreakable || !bl->upval);
  lua_assert(bl->nactvar == fs->nactvar);
  fs->freereg = fs->nactvar;  /* free registers */
  luaK_patchtohere(fs, bl->breaklist);
}


static void pushclosure (LexState *lex, FuncState *child, Expr *var) {
  FuncState *parent = lex->funcstate;
  Proto *proto = parent->proto;
  int oldsize = proto->size_children;
  int i;

  luaM_growvector(lex->L, proto->children, parent->nchildren, proto->size_children,
    Proto *, MAXARG_Bx, "constant table overflow");

  while (oldsize < proto->size_children) {
    proto->children[oldsize++] = NULL;
  }

  proto->children[parent->nchildren++] = child->proto;
  luaC_objbarrier(lex->L, proto, child->proto);
  init_exp(var, Expr_Relocable, luaK_codeABx(parent, OP_CLOSURE, 0,
           parent->nchildren - 1));

  for (i = 0; i < child->proto->nups; i++) {
    OpCode op = (child->upvalues[i].k == Expr_Local) ? OP_MOVE : OP_GETUPVAL;
    luaK_codeABC(parent, op, 0, child->upvalues[i].info, 0);
  }
}


static void open_func (LexState *lex, FuncState *fs) {
  lua_State *L = lex->L;
  Proto *proto = luaF_newproto(L);
  fs->proto = proto;
  fs->prev = lex->funcstate;  /* linked list of `FuncState *` */
  fs->lexstate = lex;
  fs->L = L;
  lex->funcstate = fs;
  fs->pc = 0;
  fs->lasttarget = -1;
  fs->jpc = NO_JUMP;
  fs->freereg = 0;
  fs->nconstants = 0;
  fs->nchildren = 0;
  fs->nlocvars = 0;
  fs->nactvar = 0;
  fs->bl = NULL;
  proto->source = lex->source;
  proto->maxstacksize = 2;  /* registers 0/1 are always valid */
  fs->h = luaH_new(L, 0, 0);
  /* anchor table of constants and prototype (to avoid being collected) */
  sethvalue2s(L, L->top, fs->h);
  incr_top(L);
  setptvalue2s(L, L->top, proto);
  incr_top(L);
}


static void close_func (LexState *lex) {
  lua_State *L = lex->L;
  FuncState *fs = lex->funcstate;
  Proto *proto = fs->proto;

  /* Set the `endpc` of all remaining locals and free their registers */
  removevars(lex, 0);
  luaK_ret(fs, 0, 0);  /* final return */

  /* shrink `proto->code` and `proto->lineinfo` to contain exactly `func->pc`
    instructions */
  luaM_reallocvector(L, proto->code, proto->size_code, fs->pc, Instruction);
  proto->size_code = fs->pc;
  luaM_reallocvector(L, proto->lineinfo, proto->size_lineinfo, fs->pc, int);
  proto->size_lineinfo = fs->pc;

  /* shrink `proto->constants` to contain exactly `func->nconstants` values */
  luaM_reallocvector(L, proto->constants, proto->size_constants,
    fs->nconstants, TValue);
  proto->size_constants = fs->nconstants;

  /* shrink `proto->children` to contain exactyl `func->nchildren` nested
    functions */
  luaM_reallocvector(L, proto->children, proto->size_children, fs->nchildren,
                     Proto *);
  proto->size_children = fs->nchildren;

  /* shrink `proto->locvars` to contain exactly `func->nlocvars` locals */
  luaM_reallocvector(L, proto->locvars, proto->size_locvars, fs->nlocvars,
                     LocVar);
  proto->size_locvars = fs->nlocvars;

  /* shrink `proto->upvalues` to contain exactly `proto->nups` nupvalues */
  luaM_reallocvector(L, proto->upvalues, proto->size_upvalues, proto->nups,
                     TString *);
  proto->size_upvalues = proto->nups;

  lua_assert(luaG_checkcode(proto));
  lua_assert(fs->bl == NULL);
  lex->funcstate = fs->prev;
  /* last token read was anchored in defunct function; must reanchor it */
  if (fs) {
    anchor_token(lex);
  }
  L->top -= 2;  /* remove table and prototype from the stack */
}


Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff, const char *name) {
  struct LexState lexstate;
  struct FuncState funcstate;
  lexstate.buff = buff;
  luaX_setinput(L, &lexstate, z, luaS_new(L, name));
  open_func(&lexstate, &funcstate);
  funcstate.proto->is_vararg = VARARG_ISVARARG;  /* main func. is always vararg */
  luaX_next(&lexstate);  /* read first token */
  chunk(&lexstate);
  check(&lexstate, Token_Eos);
  close_func(&lexstate);
  lua_assert(funcstate.prev == NULL);
  lua_assert(funcstate.proto->nups == 0);
  lua_assert(lexstate.funcstate == NULL);
  return funcstate.proto;
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


static void field (LexState *lex, Expr *var) {
  /* field -> ['.' | ':'] NAME */
  FuncState *fs = lex->funcstate;
  Expr key;
  luaK_exp2anyreg(fs, var);
  luaX_next(lex);  /* skip the dot or colon */
  checkname(lex, &key);
  luaK_indexed(fs, var, &key);
}


static void yindex (LexState *lex, Expr *var) {
  /* index -> '[' expr ']' */
  luaX_next(lex);  /* skip the '[' */
  expression(lex, var);
  luaK_exp2val(lex->funcstate, var);
  check_next(lex, Token_Right_Bracket);
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
  Expr value;  /* last list item read */
  Expr *table;  /* table descriptor */
  int nhash;  /* total number of `record' elements */
  int narray;  /* total number of array elements */
  int tostore;  /* number of array elements pending to be stored */
};


static void recfield (LexState *lex, struct ConsControl *cc) {
  /* recfield -> (NAME | `['exp1`]') = exp1 */
  FuncState *fs = lex->funcstate;
  int reg = lex->funcstate->freereg;
  Expr key, val;
  int rkkey;
  if (lex->current.type == Token_Name) {
    luaY_checklimit(fs, cc->nhash, MAX_INT, "items in a constructor");
    checkname(lex, &key);
  }
  else  /* lex->t.token == '[' */
    yindex(lex, &key);
  cc->nhash++;
  check_next(lex, Token_Assign);
  rkkey = luaK_exp2RK(fs, &key);
  expression(lex, &val);
  luaK_codeABC(fs, OP_SETTABLE, cc->table->u.s.info, rkkey, luaK_exp2RK(fs, &val));
  fs->freereg = reg;  /* free registers */
}


static void closelistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->value.kind == Expr_Void) {
    return;  /* there is no list item */
  }
  luaK_exp2nextreg(fs, &cc->value);
  cc->value.kind = Expr_Void;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    luaK_setlist(fs, cc->table->u.s.info, cc->narray, cc->tostore);  /* flush */
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->value.kind)) {
    luaK_setmultret(fs, &cc->value);
    luaK_setlist(fs, cc->table->u.s.info, cc->narray, LUA_MULTRET);
    cc->narray--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->value.kind != Expr_Void)
      luaK_exp2nextreg(fs, &cc->value);
    luaK_setlist(fs, cc->table->u.s.info, cc->narray, cc->tostore);
  }
}


static void listfield (LexState *lex, struct ConsControl *cc) {
  expression(lex, &cc->value);
  luaY_checklimit(lex->funcstate, cc->narray, MAX_INT, "items in a constructor");
  cc->narray++;
  cc->tostore++;
}


static void constructor (LexState *lex, Expr *t) {
  /* constructor -> ?? */
  FuncState *fs = lex->funcstate;
  int line = lex->linenumber;
  int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
  Instruction *ip;  /* don't initialize yet; `proto.code` may change */
  struct ConsControl cc;
  cc.narray  = 0;
  cc.nhash   = 0;
  cc.tostore = 0;
  cc.table   = t;
  init_exp(t, Expr_Relocable, pc);
  init_exp(&cc.value, Expr_Void, 0);  /* no value (yet) */
  luaK_exp2nextreg(lex->funcstate, t);  /* fix it at stack top (for gc) */
  check_next(lex, Token_Left_Curly);
  do {
    lua_assert(cc.value.kind == Expr_Void || cc.tostore > 0);
    if (lex->current.type == Token_Right_Curly) break;
    closelistfield(fs, &cc);
    switch(lex->current.type) {
      case Token_Name: {  /* may be listfields or recfields */
        luaX_lookahead(lex);
        if (lex->lookahead.type != Token_Assign)  /* expression? */
          listfield(lex, &cc);
        else
          recfield(lex, &cc);
        break;
      }
      case Token_Left_Bracket: {  /* constructor_item -> recfield */
        recfield(lex, &cc);
        break;
      }
      default: {  /* constructor_part -> listfield */
        listfield(lex, &cc);
        break;
      }
    }
  } while (test_next(lex, Token_Comma) || test_next(lex, Token_Semi));
  check_match(lex, Token_Right_Curly, Token_Left_Curly, line);
  lastlistfield(fs, &cc);
  ip = &fs->proto->code[pc];
  SETARG_B(*ip, luaO_int2fb(cc.narray)); /* set initial array size */
  SETARG_C(*ip, luaO_int2fb(cc.nhash));  /* set initial table size */
}

/* }====================================================================== */



static void parlist (LexState *lex) {
  /* parlist -> [ param { `,' param } ] */
  FuncState *fs = lex->funcstate;
  Proto *proto = fs->proto;
  int nparams = 0;
  proto->is_vararg = 0;
  if (lex->current.type != Token_Right_Paren) {  /* is `parlist' not empty? */
    do {
      switch (lex->current.type) {
        case Token_Name: {  /* param -> NAME */
          new_localvar(lex, str_checkname(lex), nparams++);
          break;
        }
        case Token_Vararg: {  /* param -> `...' */
          luaX_next(lex);
#if defined(LUA_COMPAT_VARARG)
          /* use `arg' as default name */
          new_localvarliteral(lex, "arg", nparams++);
          proto->is_vararg = VARARG_HASARG | VARARG_NEEDSARG;
#endif
          proto->is_vararg |= VARARG_ISVARARG;
          break;
        }
        default: luaX_syntaxerror(lex, "<name> or " LUA_QL("...") " expected");
      }
    } while (!proto->is_vararg && test_next(lex, Token_Comma));
  }
  adjustlocalvars(lex, nparams);
  proto->numparams = cast_byte(fs->nactvar - (proto->is_vararg & VARARG_HASARG));
  luaK_reserveregs(fs, fs->nactvar);  /* reserve register for parameters */
}


static void body (LexState *lex, Expr *expr, bool needself, int line) {
  /* body ->  `(' parlist `)' chunk END */
  FuncState new_fs;
  open_func(lex, &new_fs);
  new_fs.proto->linedefined = line;
  check_next(lex, Token_Left_Paren);
  if (needself) {
    new_localvarliteral(lex, "self", 0);
    adjustlocalvars(lex, 1);
  }
  parlist(lex);
  check_next(lex, Token_Right_Paren);
  chunk(lex);
  new_fs.proto->lastlinedefined = lex->linenumber;
  check_match(lex, Token_End, Token_Function, line);
  close_func(lex);
  pushclosure(lex, &new_fs, expr);
}


static int explist1 (LexState *lex, Expr *var) {
  /* explist1 -> expr { `,' expr } */
  int n = 1;  /* at least one expression */
  expression(lex, var);
  while (test_next(lex, Token_Comma)) {
    luaK_exp2nextreg(lex->funcstate, var);
    expression(lex, var);
    n++;
  }
  return n;
}


static void funcargs (LexState *lex, Expr *expr) {
  FuncState *fs = lex->funcstate;
  Expr args;
  int base, nparams;
  int line = lex->linenumber;
  switch (lex->current.type) {
    case Token_Left_Paren: {  /* funcargs -> `(' [ explist1 ] `)' */
      if (line != lex->lastline)
        luaX_syntaxerror(lex,"ambiguous syntax (function call x new statement)");
      luaX_next(lex);
      if (lex->current.type == Token_Right_Paren)  /* arg list is empty? */
        args.kind = Expr_Void;
      else {
        explist1(lex, &args);
        luaK_setmultret(fs, &args);
      }
      check_match(lex, Token_Right_Paren, Token_Left_Paren, line);
      break;
    }
    case Token_Left_Curly: {  /* funcargs -> constructor */
      constructor(lex, &args);
      break;
    }
    case Token_String: {  /* funcargs -> STRING */
      codestring(lex, &args, lex->current.seminfo.ts);
      luaX_next(lex);  /* must use `seminfo' before `next' */
      break;
    }
    default: {
      luaX_syntaxerror(lex, "function arguments expected");
      return;
    }
  }
  lua_assert(expr->kind == Expr_Nonrelocable);
  base = expr->u.s.info;  /* base register for call */
  if (hasmultret(args.kind))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.kind != Expr_Void)
      luaK_exp2nextreg(fs, &args);  /* close last argument */
    nparams = fs->freereg - (base+1);
  }
  init_exp(expr, Expr_Call, luaK_codeABC(fs, OP_CALL, base, nparams + 1, 2));
  luaK_fixline(fs, line);
  fs->freereg = base + 1; /* call remove function and arguments and leaves
                            (unless changed) one result */
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void prefixexp (LexState *lex, Expr *var) {
  /* prefixexp -> NAME | '(' expr ')' */
  switch (lex->current.type) {
    case Token_Left_Paren: {
      int line = lex->linenumber;
      luaX_next(lex);
      expression(lex, var);
      check_match(lex, Token_Right_Paren, Token_Left_Paren, line);
      luaK_dischargevars(lex->funcstate, var);
      return;
    }
    case Token_Name: {
      singlevar(lex, var);
      return;
    }
    default: {
      luaX_syntaxerror(lex, "unexpected symbol");
      return;
    }
  }
}


static void primaryexp (LexState *lex, Expr *var) {
  /* primaryexp -> prefixexp { `.' NAME | `[' exp `]' | `:' NAME funcargs | funcargs } */
  FuncState *fs = lex->funcstate;
  prefixexp(lex, var);
  for (;;) {
    switch (lex->current.type) {
      case Token_Dot: {  /* field */
        field(lex, var);
        break;
      }
      case Token_Left_Bracket: {  /* `[' exp1 `]' */
        Expr key;
        luaK_exp2anyreg(fs, var);
        yindex(lex, &key);
        luaK_indexed(fs, var, &key);
        break;
      }
      case Token_Colon: {  /* `:' NAME funcargs */
        Expr key;
        luaX_next(lex);
        checkname(lex, &key);
        luaK_self(fs, var, &key);
        funcargs(lex, var);
        break;
      }
      case Token_Left_Paren:
      case Token_String:
      case Token_Left_Curly: {  /* funcargs */
        luaK_exp2nextreg(fs, var);
        funcargs(lex, var);
        break;
      }
      default: return;
    }
  }
}


static void simpleexp (LexState *lex, Expr *var) {
  /* simpleexp -> NUMBER | STRING | NIL | true | false | ... |
                  constructor | FUNCTION body | primaryexp */
  switch (lex->current.type) {
    case Token_Number: {
      init_exp(var, Expr_Number, 0);
      var->u.nval = lex->current.seminfo.r;
      break;
    }
    case Token_String: {
      codestring(lex, var, lex->current.seminfo.ts);
      break;
    }
    case Token_Nil: {
      init_exp(var, Expr_Nil, 0);
      break;
    }
    case Token_True: {
      init_exp(var, Expr_True, 0);
      break;
    }
    case Token_False: {
      init_exp(var, Expr_False, 0);
      break;
    }
    case Token_Vararg: {
      FuncState *fs = lex->funcstate;
      check_condition(lex, fs->proto->is_vararg,
                      "cannot use " LUA_QL("...") " outside a vararg function");
      fs->proto->is_vararg &= ~VARARG_NEEDSARG;  /* don't need 'arg' */
      init_exp(var, Expr_Vararg, luaK_codeABC(fs, OP_VARARG, 0, 1, 0));
      break;
    }
    case Token_Left_Curly: {  /* constructor */
      constructor(lex, var);
      return;
    }
    case Token_Function: {
      luaX_next(lex);
      body(lex, var, 0, lex->linenumber);
      return;
    }
    default: {
      primaryexp(lex, var);
      return;
    }
  }
  luaX_next(lex);
}


static UnOpr getunopr (TokenType op) {
  switch (op) {
    case Token_Not: return OPR_NOT;
    case Token_Sub: return OPR_MINUS;
    case Token_Len: return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (TokenType op) {
  switch (op) {
    case Token_Add: return OPR_ADD;
    case Token_Sub: return OPR_SUB;
    case Token_Mul: return OPR_MUL;
    case Token_Div: return OPR_DIV;
    case Token_Mod: return OPR_MOD;
    case Token_Pow: return OPR_POW;
    case Token_Concat: return OPR_CONCAT;
    case Token_Neq: return OPR_NE;
    case Token_Eq: return OPR_EQ;
    case Token_Lt: return OPR_LT;
    case Token_Leq: return OPR_LE;
    case Token_Gt: return OPR_GT;
    case Token_Geq: return OPR_GE;
    case Token_And: return OPR_AND;
    case Token_Or: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}


static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* `+' `-' `/' `%' */
   {10, 9}, {5, 4},                 /* power and concat (right associative) */
   {3, 3}, {3, 3},                  /* equality and inequality */
   {3, 3}, {3, 3}, {3, 3}, {3, 3},  /* order */
   {2, 2}, {1, 1}                   /* logical (and/or) */
};

#define UNARY_PRIORITY	8  /* priority for unary operators */

/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where `binop' is any binary operator with a priority higher than `limit'
*/
static BinOpr subexpr (LexState *lex, Expr *var, unsigned int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(lex);
  uop = getunopr(lex->current.type);
  if (uop != OPR_NOUNOPR) {
    luaX_next(lex);
    subexpr(lex, var, UNARY_PRIORITY);
    luaK_prefix(lex->funcstate, uop, var);
  }
  else simpleexp(lex, var);
  /* expand while operators have priorities higher than `limit' */
  op = getbinopr(lex->current.type);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    Expr v2;
    BinOpr nextop;
    luaX_next(lex);
    luaK_infix(lex->funcstate, op, var);
    /* read sub-expression with higher priority */
    nextop = subexpr(lex, &v2, priority[op].right);
    luaK_posfix(lex->funcstate, op, var, &v2);
    op = nextop;
  }
  leavelevel(lex);
  return op;  /* return first untreated operator */
}


/**
 * @note 2025-04-07:
 *  Originally named `expr`, renamed to `expression` so that we have less
 *  conflicts when using it as a variable name.
 */
static void expression (LexState *lex, Expr *var) {
  subexpr(lex, var, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static bool block_follow (TokenType type) {
  switch (type) {
    case Token_Else:
    case Token_Elseif:
    case Token_End:
    case Token_Until:
    case Token_Eos:
      return true;
    default:
      return false;
  }
}


static void block (LexState *lex) {
  /* block -> chunk */
  FuncState *fs = lex->funcstate;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  chunk(lex);
  lua_assert(bl.breaklist == NO_JUMP);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  Expr var;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to a local variable, the local variable
** is needed in a previous assignment (to a table). If so, save original
** local value in a safe place and use this safe copy in the previous
** assignment.
*/
static void check_conflict (LexState *lex, struct LHS_assign *lh, Expr *var) {
  FuncState *fs = lex->funcstate;
  int extra = fs->freereg;  /* eventual position to save local variable */
  bool conflict = false;
  for (; lh; lh = lh->prev) {
    if (lh->var.kind == Expr_Index) {
      if (lh->var.u.s.info == var->u.s.info) {  /* conflict? */
        conflict = true;
        /* init_exp-like */
        lh->var.u.s.info = extra;  /* previous assignment will use safe copy */
      }
      if (lh->var.u.s.aux == var->u.s.info) {  /* conflict? */
        conflict = true;
        /* init_exp-like */
        lh->var.u.s.aux = extra;  /* previous assignment will use safe copy */
      }
    }
  }
  if (conflict) {
    luaK_codeABC(fs, OP_MOVE, fs->freereg, var->u.s.info, 0);  /* make copy */
    luaK_reserveregs(fs, 1);
  }
}


static void assignment (LexState *lex, struct LHS_assign *lh, int nvars) {
  Expr expr;
  check_condition(lex, Expr_Local <= lh->var.kind && lh->var.kind <= Expr_Index,
      "syntax error");

  if (test_next(lex, Token_Comma)) {  /* assignment -> `,' primaryexp assignment */
    struct LHS_assign next;
    next.prev = lh;
    primaryexp(lex, &next.var);
    if (next.var.kind == Expr_Local)
      check_conflict(lex, lh, &next.var);
    luaY_checklimit(lex->funcstate, nvars, LUAI_MAXCCALLS - lex->L->nCcalls,
      "variables in assignment");
    assignment(lex, &next, nvars + 1);
  }
  else {  /* assignment -> `=' explist1 */
    int nexps;
    check_next(lex, Token_Assign);
    nexps = explist1(lex, &expr);
    if (nexps != nvars) {
      adjust_assign(lex, nvars, nexps, &expr); /* may push `expr` */
      if (nexps > nvars)
        lex->funcstate->freereg -= (nexps - nvars);  /* remove extra values */
    }
    else {
      luaK_setoneret(lex->funcstate, &expr);  /* close last expression */
      luaK_storevar(lex->funcstate, &lh->var, &expr);
      return;  /* avoid default */
    }
  }
  /**
   * @details
   *  - Each recursive call will end up calling `luaK_storevar().
   *  - This decrements `lex->func->freereg` whenever we have a register that
   *    does not refer to a local.
   *  - This is usually the case in `explist()` pushes all expressions in order
   *    except for the last.
   *  - Thus, we can assume that the current top of the stack is where our
   *    desired value (for this assignment target) lies.
   */
  init_exp(&expr, Expr_Nonrelocable, lex->funcstate->freereg-1);
  luaK_storevar(lex->funcstate, &lh->var, &expr);
}


static int cond (LexState *lex) {
  /* cond -> exp */
  Expr expr;
  expression(lex, &expr);  /* read condition */
  if (expr.kind == Expr_Nil)
    expr.kind = Expr_False;  /* `falses' are all equal here */
  luaK_goiftrue(lex->funcstate, &expr);
  return expr.patch_false;
}


static void break_stmt (LexState *lex) {
  FuncState *fs    = lex->funcstate;
  BlockCnt  *bl    = fs->bl;
  bool       upval = false;
  while (bl && !bl->isbreakable) {
    upval |= bl->upval; /* equivalent to: upval = upval || bl->upval; */
    bl = bl->previous;
  }
  if (!bl)
    luaX_syntaxerror(lex, "no loop to break");
  if (upval)
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
  luaK_concat(fs, &bl->breaklist, luaK_jump(fs));
}


static void while_stmt (LexState *lex, int line) {
  /* while_stmt -> WHILE cond DO block END */
  FuncState *fs = lex->funcstate;
  int whileinit;
  int condexit;
  BlockCnt bl;
  luaX_next(lex);  /* skip WHILE */
  whileinit = luaK_getlabel(fs);
  condexit = cond(lex);
  enterblock(fs, &bl, 1);
  check_next(lex, Token_Do);
  block(lex);
  luaK_patchlist(fs, luaK_jump(fs), whileinit);
  check_match(lex, Token_End, Token_While, line);
  leaveblock(fs);
  luaK_patchtohere(fs, condexit);  /* false conditions finish the loop */
}


static void repeat_stmt (LexState *lex, int line) {
  /* repeat_stmt -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = lex->funcstate;
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  luaX_next(lex);  /* skip REPEAT */
  chunk(lex);
  check_match(lex, Token_Until, Token_Repeat, line);
  condexit = cond(lex);  /* read condition (inside scope block) */
  if (!bl2.upval) {  /* no upvalues? */
    leaveblock(fs);  /* finish scope */
    luaK_patchlist(fs, condexit, repeat_init);  /* close the loop */
  }
  else {  /* complete semantics when there are upvalues */
    break_stmt(lex);  /* if condition then break */
    luaK_patchtohere(fs, condexit);  /* else... */
    leaveblock(fs);  /* finish scope... */
    luaK_patchlist(fs, luaK_jump(fs), repeat_init);  /* and repeat */
  }
  leaveblock(fs);  /* finish loop */
}


static int exp1 (LexState *lex) {
  Expr expr;
  int kind;
  expression(lex, &expr);
  kind = expr.kind;
  luaK_exp2nextreg(lex->funcstate, &expr);
  return kind;
}


static void for_body (LexState *lex, int base, int line, int nvars, bool is_num) {
  /* for_body -> DO block */
  BlockCnt bl;
  FuncState *fs = lex->funcstate;
  int prep, endfor;
  adjustlocalvars(lex, 3);  /* control variables */
  check_next(lex, Token_Do);
  prep = is_num ? luaK_codeAsBx(fs, OP_FORPREP, base, NO_JUMP) : luaK_jump(fs);
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(lex, nvars);
  luaK_reserveregs(fs, nvars);
  block(lex);
  leaveblock(fs);  /* end of scope for declared variables */
  luaK_patchtohere(fs, prep);
  endfor = (is_num)
    ? luaK_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP)
    : luaK_codeABC(fs, OP_TFORLOOP, base, 0, nvars);

  luaK_fixline(fs, line);  /* pretend that `OP_FOR' starts the loop */
  luaK_patchlist(fs, (is_num ? endfor : luaK_jump(fs)), prep + 1);
}


static void for_num (LexState *lex, TString *varname, int line) {
  /* for_num -> NAME = exp1,exp1[,exp1] for_body */
  FuncState *fs = lex->funcstate;
  int base = fs->freereg;
  new_localvarliteral(lex, "(for index)", 0);
  new_localvarliteral(lex, "(for limit)", 1);
  new_localvarliteral(lex, "(for step)", 2);
  new_localvar(lex, varname, 3);
  check_next(lex, Token_Assign);
  exp1(lex);  /* initial value */
  check_next(lex, Token_Comma);
  exp1(lex);  /* limit */
  if (test_next(lex, Token_Comma))
    exp1(lex);  /* optional step */
  else {  /* default step = 1 */
    luaK_codeABx(fs, OP_LOADK, fs->freereg, luaK_numberK(fs, 1));
    luaK_reserveregs(fs, 1);
  }
  for_body(lex, base, line, 1, true);
}


static void for_list (LexState *lex, TString *indexname) {
  /* for_list -> NAME {,NAME} IN explist1 for_body */
  FuncState *fs = lex->funcstate;
  Expr e;
  int nvars = 0;
  int line;
  int base = fs->freereg;
  /* create control variables */
  new_localvarliteral(lex, "(for generator)", nvars++);
  new_localvarliteral(lex, "(for state)", nvars++);
  new_localvarliteral(lex, "(for control)", nvars++);
  /* create declared variables */
  new_localvar(lex, indexname, nvars++);
  while (test_next(lex, Token_Comma))
    new_localvar(lex, str_checkname(lex), nvars++);
  check_next(lex, Token_In);
  line = lex->linenumber;
  adjust_assign(lex, 3, explist1(lex, &e), &e);
  luaK_checkstack(fs, 3);  /* extra space to call generator */
  for_body(lex, base, line, nvars - 3, false);
}


static void for_stmt (LexState *lex, int line) {
  /* for_stmt -> FOR (for_num | for_list) END */
  FuncState *fs = lex->funcstate;
  TString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  luaX_next(lex);  /* skip `for' */
  varname = str_checkname(lex);  /* first variable name */
  switch (lex->current.type) {
    case Token_Assign: for_num(lex, varname, line); break;
    case Token_Comma:
    case Token_In: for_list(lex, varname); break;
    default: luaX_syntaxerror(lex, LUA_QL("=") " or " LUA_QL("in") " expected");
  }
  check_match(lex, Token_End, Token_For, line);
  leaveblock(fs);  /* loop scope (`break' jumps to this point) */
}

/* returns the pc of the jump instruction that comes after the test */
static int test_then_block (LexState *lex) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  int condexit;
  luaX_next(lex);  /* skip IF or ELSEIF */
  condexit = cond(lex);
  check_next(lex, Token_Then);
  block(lex);  /* `then' part */
  return condexit;
}


static void if_stmt (LexState *lex, int line) {
  /* if_stmt -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *fs = lex->funcstate;
  int flist;
  int escapelist = NO_JUMP;
  flist = test_then_block(lex);  /* IF cond THEN block */
  while (lex->current.type == Token_Elseif) {
    luaK_concat(fs, &escapelist, luaK_jump(fs)); /* assigns `escapelist` */
    luaK_patchtohere(fs, flist); /* definitely assigns `func.jpc` */
    flist = test_then_block(lex);  /* ELSEIF cond THEN block */
  }
  if (lex->current.type == Token_Else) {
    luaK_concat(fs, &escapelist, luaK_jump(fs)); /* assigns `escapelist` */
    luaK_patchtohere(fs, flist); /* definitely assigns `func.jpc` */
    luaX_next(lex);  /* skip ELSE (after patch, for correct line info) */
    block(lex);  /* `else' part */
  }
  else {
    luaK_concat(fs, &escapelist, flist); /* assigns `escapelist` */
  }
  luaK_patchtohere(fs, escapelist);
  check_match(lex, Token_End, Token_If, line);
}


static void local_func (LexState *lex) {
  Expr name, fbody;
  FuncState *fs = lex->funcstate;
  new_localvar(lex, str_checkname(lex), 0);
  init_exp(&name, Expr_Local, fs->freereg);
  luaK_reserveregs(fs, 1);
  adjustlocalvars(lex, 1);
  body(lex, &fbody, 0, lex->linenumber);
  luaK_storevar(fs, &name, &fbody);
  /* debug information will only see the variable after this point! */
  getlocvar(fs, fs->nactvar - 1).startpc = fs->pc;
}


static void local_stmt (LexState *lex) {
  /* stat -> LOCAL NAME {`,' NAME} [`=' explist1] */
  int nvars = 0;
  int nexps;
  Expr exprs; /* assigning expression/s, if any */
  do {
    new_localvar(lex, str_checkname(lex), nvars++);
  } while (test_next(lex, Token_Comma));
  if (test_next(lex, Token_Assign)) {
    nexps = explist1(lex, &exprs);
  }
  else {
    exprs.kind = Expr_Void;
    nexps = 0;
  }
  adjust_assign(lex, nvars, nexps, &exprs);
  adjustlocalvars(lex, nvars);
}


static bool funcname (LexState *lex, Expr *var) {
  /* funcname -> NAME {field} [`:' NAME] */
  bool needself = false;
  singlevar(lex, var);
  while (lex->current.type == Token_Dot) {
    field(lex, var);
  }

  if (lex->current.type == Token_Colon) {
    needself = true;
    field(lex, var);
  }
  return needself;
}


static void func_stmt (LexState *lex, int line) {
  /* func_stmt -> FUNCTION funcname body */
  bool needself;
  Expr var, fbody;
  FuncState *fs = lex->funcstate;
  luaX_next(lex);  /* skip FUNCTION */
  needself = funcname(lex, &var);
  body(lex, &fbody, needself, line);
  luaK_storevar(fs, &var, &fbody);
  luaK_fixline(fs, line);  /* definition `happens' in the first line */
}


static void expr_stmt (LexState *lex) {
  /* stat -> func | assignment */
  FuncState *fs = lex->funcstate;
  struct LHS_assign v;
  primaryexp(lex, &v.var);
  if (v.var.kind == Expr_Call) { /* stat -> func */
    Instruction *ip = getcode(fs, &v.var);
    SETARG_C(*ip, 1);  /* call statement uses no results */
  } else {  /* stat -> assignment */
    v.prev = NULL;
    assignment(lex, &v, 1);
  }
}


static void return_stmt (LexState *lex) {
  /* stat -> RETURN explist */
  FuncState *fs = lex->funcstate;
  Expr expr;
  int first, nret;  /* registers with returned values */
  luaX_next(lex);  /* skip RETURN */
  if (block_follow(lex->current.type) || lex->current.type == Token_Semi)
    first = nret = 0;  /* return no values */
  else {
    nret = explist1(lex, &expr);  /* optional return values */
    if (hasmultret(expr.kind)) {
      luaK_setmultret(fs, &expr);
      if (expr.kind == Expr_Call && nret == 1) {  /* tail call? */
        Instruction *ip = getcode(fs, &expr);
        SET_OPCODE(*ip, OP_TAILCALL);
        lua_assert(GETARG_A(*ip) == fs->nactvar);
      }
      first = fs->nactvar;
      nret  = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1) {  /* only one single value? */
        first = luaK_exp2anyreg(fs, &expr);
      }
      else {
        luaK_exp2nextreg(fs, &expr);  /* values must go to the `stack' */
        first = fs->nactvar;  /* return all `active' values */
        lua_assert(nret == fs->freereg - first);
      }
    }
  }
  luaK_ret(fs, first, nret);
}


static bool statement (LexState *lex) {
  int line = lex->linenumber;  /* may be needed for error messages */
  switch (lex->current.type) {
    case Token_If: {  /* stat -> if_stmt */
      if_stmt(lex, line);
      return false;
    }
    case Token_While: {  /* stat -> while_stmt */
      while_stmt(lex, line);
      return false;
    }
    case Token_Do: {  /* stat -> DO block END */
      luaX_next(lex);  /* skip DO */
      block(lex);
      check_match(lex, Token_End, Token_Do, line);
      return false;
    }
    case Token_For: {  /* stat -> for_stmt */
      for_stmt(lex, line);
      return false;
    }
    case Token_Repeat: {  /* stat -> repeat_stmt */
      repeat_stmt(lex, line);
      return false;
    }
    case Token_Function: {
      func_stmt(lex, line);  /* stat -> func_stmt */
      return false;
    }
    case Token_Local: {  /* stat -> local_stmt */
      luaX_next(lex);  /* skip LOCAL */
      if (test_next(lex, Token_Function))  /* local function? */
        local_func(lex);
      else
        local_stmt(lex);
      return false;
    }
    case Token_Return: {  /* stat -> return_stmt */
      return_stmt(lex);
      return true;  /* must be last statement */
    }
    case Token_Break: {  /* stat -> break_stmt */
      luaX_next(lex);  /* skip BREAK */
      break_stmt(lex);
      return true;  /* must be last statement */
    }
    default: {
      expr_stmt(lex);
      return false;  /* to avoid warnings */
    }
  }
}


static void chunk (LexState *lex) {
  /* chunk -> { stat [`;'] } */
  bool is_last = false;

  /**
   * @todo 2025-05-12:
   *  Check if this can be invalidated! Although I'm assuming that this is
   *  valid between any call-pair of `open_func()` and `close_func()`.
   */
  FuncState *fs = lex->funcstate;
  enterlevel(lex);
  while (!is_last && !block_follow(lex->current.type)) {
    is_last = statement(lex);
    test_next(lex, Token_Semi);
    lua_assert(fs->proto->maxstacksize >= fs->freereg
            && fs->freereg >= fs->nactvar);
    fs->freereg = fs->nactvar;  /* free registers */
  }
  leavelevel(lex);
}

/* }====================================================================== */
