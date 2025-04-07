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



#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)

#define getlocvar(fs, i)	((fs)->proto->locvars[(fs)->actvar[i]])

#define luaY_checklimit(fs,v,l,m)	if ((v)>(l)) errorlimit(fs,l,m)


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* chain */
  int breaklist;  /* list of jumps out of this loop */
  lu_byte nactvar;  /* # active locals outside the breakable structure */
  lu_byte upval;  /* true if some variable in the block is an upvalue */
  lu_byte isbreakable;  /* true if `block' is a loop */
} BlockCnt;



/*
** prototypes for recursive non-terminal functions
*/
static void chunk (LexState *lex);
static void expression (LexState *lex, Expr *v);


static void anchor_token (LexState *lex) {
  if (lex->current.token == TK_NAME || lex->current.token == TK_STRING) {
    TString *ts = lex->current.seminfo.ts;
    luaX_newstring(lex, getstr(ts), ts->tsv.len);
  }
}


static void error_expected (LexState *lex, int token) {
  luaX_syntaxerror(lex,
      luaO_pushfstring(lex->L, LUA_QS " expected", luaX_token2str(lex, token)));
}


static void errorlimit (FuncState *func, int limit, const char *what) {
  const char *msg = (func->proto->linedefined == 0) ?
    luaO_pushfstring(func->L, "main function has more than %d %s", limit, what) :
    luaO_pushfstring(func->L, "function at line %d has more than %d %s",
                            func->proto->linedefined, limit, what);
  luaX_lexerror(func->lex, msg, 0);
}


static int testnext (LexState *lex, int c) {
  if (lex->current.token == c) {
    luaX_next(lex);
    return 1;
  }
  else return 0;
}


static void check (LexState *lex, int c) {
  if (lex->current.token != c)
    error_expected(lex, c);
}

static void checknext (LexState *lex, int c) {
  check(lex, c);
  luaX_next(lex);
}


#define check_condition(ls,c,msg)	{ if (!(c)) luaX_syntaxerror(ls, msg); }



static void check_match (LexState *lex, int what, int who, int where) {
  if (!testnext(lex, what)) {
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
  check(lex, TK_NAME);
  ts = lex->current.seminfo.ts;
  luaX_next(lex);
  return ts;
}


static void init_exp (Expr *e, ExprKind k, int i) {
  e->f = e->t = NO_JUMP;
  e->kind = k;
  e->u.s.info = i;
}


static void codestring (LexState *lex, Expr *e, TString *s) {
  init_exp(e, VK, luaK_stringK(lex->func, s));
}


static void checkname(LexState *lex, Expr *e) {
  codestring(lex, e, str_checkname(lex));
}


static int registerlocalvar (LexState *lex, TString *varname) {
  FuncState *func = lex->func;
  Proto *proto = func->proto;
  int oldsize = proto->size_locvars;
  luaM_growvector(lex->L, proto->locvars, func->nlocvars, proto->size_locvars,
    LocVar, SHRT_MAX, "too many local variables");

  while (oldsize < proto->size_locvars) 
    proto->locvars[oldsize++].varname = NULL;
  proto->locvars[func->nlocvars].varname = varname;
  luaC_objbarrier(lex->L, proto, varname);
  return func->nlocvars++;
}


#define new_localvarliteral(ls,v,n) \
  new_localvar(lex, luaX_newstring(lex, "" v, (sizeof(v)/sizeof(char))-1), n)


static void new_localvar (LexState *lex, TString *name, int n) {
  FuncState *func = lex->func;
  luaY_checklimit(func, func->nactvar+n+1, LUAI_MAXVARS, "local variables");
  func->actvar[func->nactvar+n] = cast(unsigned short, registerlocalvar(lex, name));
}


static void adjustlocalvars (LexState *lex, int nvars) {
  FuncState *func = lex->func;
  func->nactvar = cast_byte(func->nactvar + nvars);
  for (; nvars; nvars--) {
    getlocvar(func, func->nactvar - nvars).startpc = func->pc;
  }
}


static void removevars (LexState *lex, int tolevel) {
  FuncState *func = lex->func;
  while (func->nactvar > tolevel)
    getlocvar(func, --func->nactvar).endpc = func->pc;
}


static int indexupvalue (FuncState *func, TString *name, Expr *v) {
  int i;
  Proto *proto = func->proto;
  int oldsize = proto->size_upvalues;
  for (i=0; i<proto->nups; i++) {
    if (func->upvalues[i].k == v->kind && func->upvalues[i].info == v->u.s.info) {
      lua_assert(proto->upvalues[i] == name);
      return i;
    }
  }
  /* new one */
  luaY_checklimit(func, proto->nups + 1, LUAI_MAXUPVALUES, "upvalues");
  luaM_growvector(func->L, proto->upvalues, proto->nups, proto->size_upvalues,
    TString *, MAX_INT, "");

  while (oldsize < proto->size_upvalues)
    proto->upvalues[oldsize++] = NULL;
  proto->upvalues[proto->nups] = name;
  luaC_objbarrier(func->L, proto, name);
  lua_assert(v->kind == VLOCAL || v->kind == VUPVAL);
  func->upvalues[proto->nups].k = cast_byte(v->kind);
  func->upvalues[proto->nups].info = cast_byte(v->u.s.info);
  return proto->nups++;
}


static int searchvar (FuncState *func, TString *n) {
  int i;
  for (i=func->nactvar-1; i >= 0; i--) {
    if (n == getlocvar(func, i).varname)
      return i;
  }
  return -1;  /* not found */
}


static void markupval (FuncState *func, int level) {
  BlockCnt *bl = func->bl;
  while (bl && bl->nactvar > level) bl = bl->previous;
  if (bl)
    bl->upval = 1;
}


static int singlevaraux (FuncState *func, TString *n, Expr *var, int base) {
  if (func == NULL) {  /* no more levels? */
    init_exp(var, VGLOBAL, NO_REG);  /* default is global variable */
    return VGLOBAL;
  }
  else {
    int v = searchvar(func, n);  /* look up at current level */
    if (v >= 0) {
      init_exp(var, VLOCAL, v);
      if (!base)
        markupval(func, v);  /* local will be used as an upval */
      return VLOCAL;
    }
    else {  /* not found at current level; try upper one */
      if (singlevaraux(func->prev, n, var, 0) == VGLOBAL)
        return VGLOBAL;
      var->u.s.info = indexupvalue(func, n, var);  /* else was LOCAL or UPVAL */
      var->kind = VUPVAL;  /* upvalue in this level */
      return VUPVAL;
    }
  }
}


static void singlevar (LexState *lex, Expr *var) {
  TString *varname = str_checkname(lex);
  FuncState *func = lex->func;
  if (singlevaraux(func, varname, var, 1) == VGLOBAL)
    var->u.s.info = luaK_stringK(func, varname);  /* info points to global name */
}


static void adjust_assign (LexState *lex, int nvars, int nexps, Expr *e) {
  FuncState *func = lex->func;
  int extra = nvars - nexps;
  if (hasmultret(e->kind)) {
    extra++;  /* includes call itself */
    if (extra < 0)
      extra = 0;
    luaK_setreturns(func, e, extra);  /* last exp. provides the difference */
    if (extra > 1)
      luaK_reserveregs(func, extra-1);
  }
  else {
    if (e->kind != VVOID)
      luaK_exp2nextreg(func, e);  /* close last expression */
    if (extra > 0) {
      int reg = func->freereg;
      luaK_reserveregs(func, extra);
      luaK_nil(func, reg, extra);
    }
  }
}


static void enterlevel (LexState *lex) {
  if (++lex->L->nCcalls > LUAI_MAXCCALLS)
	luaX_lexerror(lex, "chunk has too many syntax levels", 0);
}


#define leavelevel(ls)	((ls)->L->nCcalls--)


static void enterblock (FuncState *func, BlockCnt *bl, lu_byte isbreakable) {
  bl->breaklist = NO_JUMP;
  bl->isbreakable = isbreakable;
  bl->nactvar = func->nactvar;
  bl->upval = 0;
  bl->previous = func->bl;
  func->bl = bl;
  lua_assert(func->freereg == func->nactvar);
}


static void leaveblock (FuncState *func) {
  BlockCnt *bl = func->bl;
  func->bl = bl->previous;
  removevars(func->lex, bl->nactvar);
  if (bl->upval)
    luaK_codeABC(func, OP_CLOSE, bl->nactvar, 0, 0);
  /* a block either controls scope or breaks (never both) */
  lua_assert(!bl->isbreakable || !bl->upval);
  lua_assert(bl->nactvar == func->nactvar);
  func->freereg = func->nactvar;  /* free registers */
  luaK_patchtohere(func, bl->breaklist);
}


static void pushclosure (LexState *lex, FuncState *child, Expr *v) {
  FuncState *parent = lex->func;
  Proto *proto = parent->proto;
  int oldsize = proto->size_children;
  int i;
  
  luaM_growvector(lex->L, proto->children, parent->nchildren, proto->size_children,
    Proto *, MAXARG_Bx, "constant table overflow");

  while (oldsize < proto->size_children)
    proto->children[oldsize++] = NULL;

  proto->children[parent->nchildren++] = child->proto;
  luaC_objbarrier(lex->L, proto, child->proto);
  init_exp(v, VRELOCABLE, luaK_codeABx(parent, OP_CLOSURE, 0, parent->nchildren - 1));

  for (i = 0; i < child->proto->nups; i++) {
    OpCode o = (child->upvalues[i].k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
    luaK_codeABC(parent, o, 0, child->upvalues[i].info, 0);
  }
}


static void open_func (LexState *lex, FuncState *func) {
  lua_State *L = lex->L;
  Proto *proto = luaF_newproto(L);
  func->proto = proto;
  func->prev = lex->func;  /* linked list of funcstates */
  func->lex = lex;
  func->L = L;
  lex->func = func;
  func->pc = 0;
  func->lasttarget = -1;
  func->jpc = NO_JUMP;
  func->freereg = 0;
  func->nconstants = 0;
  func->nchildren = 0;
  func->nlocvars = 0;
  func->nactvar = 0;
  func->bl = NULL;
  proto->source = lex->source;
  proto->maxstacksize = 2;  /* registers 0/1 are always valid */
  func->h = luaH_new(L, 0, 0);
  /* anchor table of constants and prototype (to avoid being collected) */
  sethvalue2s(L, L->top, func->h);
  incr_top(L);
  setptvalue2s(L, L->top, proto);
  incr_top(L);
}


static void close_func (LexState *lex) {
  lua_State *L = lex->L;
  FuncState *func = lex->func;
  Proto *proto = func->proto;
  removevars(lex, 0);
  luaK_ret(func, 0, 0);  /* final return */
  luaM_reallocvector(L, proto->code, proto->size_code, func->pc, Instruction);
  proto->size_code = func->pc;
  luaM_reallocvector(L, proto->lineinfo, proto->size_lineinfo, func->pc, int);
  proto->size_lineinfo = func->pc;
  luaM_reallocvector(L, proto->constants, proto->size_constants, func->nconstants, TValue);
  proto->size_constants = func->nconstants;
  luaM_reallocvector(L, proto->children, proto->size_children, func->nchildren, Proto *);
  proto->size_children = func->nchildren;
  luaM_reallocvector(L, proto->locvars, proto->size_locvars, func->nlocvars, LocVar);
  proto->size_locvars = func->nlocvars;
  luaM_reallocvector(L, proto->upvalues, proto->size_upvalues, proto->nups, TString *);
  proto->size_upvalues = proto->nups;
  lua_assert(luaG_checkcode(proto));
  lua_assert(func->bl == NULL);
  lex->func = func->prev;
  /* last token read was anchored in defunct function; must reanchor it */
  if (func) anchor_token(lex);
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
  check(&lexstate, TK_EOS);
  close_func(&lexstate);
  lua_assert(funcstate.prev == NULL);
  lua_assert(funcstate.proto->nups == 0);
  lua_assert(lexstate.func == NULL);
  return funcstate.proto;
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


static void field (LexState *lex, Expr *v) {
  /* field -> ['.' | ':'] NAME */
  FuncState *func = lex->func;
  Expr key;
  luaK_exp2anyreg(func, v);
  luaX_next(lex);  /* skip the dot or colon */
  checkname(lex, &key);
  luaK_indexed(func, v, &key);
}


static void yindex (LexState *lex, Expr *v) {
  /* index -> '[' expr ']' */
  luaX_next(lex);  /* skip the '[' */
  expression(lex, v);
  luaK_exp2val(lex->func, v);
  checknext(lex, ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
  Expr v;  /* last list item read */
  Expr *t;  /* table descriptor */
  int nh;  /* total number of `record' elements */
  int na;  /* total number of array elements */
  int tostore;  /* number of array elements pending to be stored */
};


static void recfield (LexState *lex, struct ConsControl *cc) {
  /* recfield -> (NAME | `['exp1`]') = exp1 */
  FuncState *func = lex->func;
  int reg = lex->func->freereg;
  Expr key, val;
  int rkkey;
  if (lex->current.token == TK_NAME) {
    luaY_checklimit(func, cc->nh, MAX_INT, "items in a constructor");
    checkname(lex, &key);
  }
  else  /* lex->t.token == '[' */
    yindex(lex, &key);
  cc->nh++;
  checknext(lex, '=');
  rkkey = luaK_exp2RK(func, &key);
  expression(lex, &val);
  luaK_codeABC(func, OP_SETTABLE, cc->t->u.s.info, rkkey, luaK_exp2RK(func, &val));
  func->freereg = reg;  /* free registers */
}


static void closelistfield (FuncState *func, struct ConsControl *cc) {
  if (cc->v.kind == VVOID) return;  /* there is no list item */
  luaK_exp2nextreg(func, &cc->v);
  cc->v.kind = VVOID;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    luaK_setlist(func, cc->t->u.s.info, cc->na, cc->tostore);  /* flush */
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *func, struct ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.kind)) {
    luaK_setmultret(func, &cc->v);
    luaK_setlist(func, cc->t->u.s.info, cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.kind != VVOID)
      luaK_exp2nextreg(func, &cc->v);
    luaK_setlist(func, cc->t->u.s.info, cc->na, cc->tostore);
  }
}


static void listfield (LexState *lex, struct ConsControl *cc) {
  expression(lex, &cc->v);
  luaY_checklimit(lex->func, cc->na, MAX_INT, "items in a constructor");
  cc->na++;
  cc->tostore++;
}


static void constructor (LexState *lex, Expr *t) {
  /* constructor -> ?? */
  FuncState *func = lex->func;
  int line = lex->linenumber;
  int pc = luaK_codeABC(func, OP_NEWTABLE, 0, 0, 0);
  struct ConsControl cc;
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  init_exp(t, VRELOCABLE, pc);
  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
  luaK_exp2nextreg(lex->func, t);  /* fix it at stack top (for gc) */
  checknext(lex, '{');
  do {
    lua_assert(cc.v.kind == VVOID || cc.tostore > 0);
    if (lex->current.token == '}') break;
    closelistfield(func, &cc);
    switch(lex->current.token) {
      case TK_NAME: {  /* may be listfields or recfields */
        luaX_lookahead(lex);
        if (lex->lookahead.token != '=')  /* expression? */
          listfield(lex, &cc);
        else
          recfield(lex, &cc);
        break;
      }
      case '[': {  /* constructor_item -> recfield */
        recfield(lex, &cc);
        break;
      }
      default: {  /* constructor_part -> listfield */
        listfield(lex, &cc);
        break;
      }
    }
  } while (testnext(lex, ',') || testnext(lex, ';'));
  check_match(lex, '}', '{', line);
  lastlistfield(func, &cc);
  SETARG_B(func->proto->code[pc], luaO_int2fb(cc.na)); /* set initial array size */
  SETARG_C(func->proto->code[pc], luaO_int2fb(cc.nh));  /* set initial table size */
}

/* }====================================================================== */



static void parlist (LexState *lex) {
  /* parlist -> [ param { `,' param } ] */
  FuncState *func = lex->func;
  Proto *proto = func->proto;
  int nparams = 0;
  proto->is_vararg = 0;
  if (lex->current.token != ')') {  /* is `parlist' not empty? */
    do {
      switch (lex->current.token) {
        case TK_NAME: {  /* param -> NAME */
          new_localvar(lex, str_checkname(lex), nparams++);
          break;
        }
        case TK_DOTS: {  /* param -> `...' */
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
    } while (!proto->is_vararg && testnext(lex, ','));
  }
  adjustlocalvars(lex, nparams);
  proto->numparams = cast_byte(func->nactvar - (proto->is_vararg & VARARG_HASARG));
  luaK_reserveregs(func, func->nactvar);  /* reserve register for parameters */
}


static void body (LexState *lex, Expr *e, int needself, int line) {
  /* body ->  `(' parlist `)' chunk END */
  FuncState new_fs;
  open_func(lex, &new_fs);
  new_fs.proto->linedefined = line;
  checknext(lex, '(');
  if (needself) {
    new_localvarliteral(lex, "self", 0);
    adjustlocalvars(lex, 1);
  }
  parlist(lex);
  checknext(lex, ')');
  chunk(lex);
  new_fs.proto->lastlinedefined = lex->linenumber;
  check_match(lex, TK_END, TK_FUNCTION, line);
  close_func(lex);
  pushclosure(lex, &new_fs, e);
}


static int explist1 (LexState *lex, Expr *v) {
  /* explist1 -> expr { `,' expr } */
  int n = 1;  /* at least one expression */
  expression(lex, v);
  while (testnext(lex, ',')) {
    luaK_exp2nextreg(lex->func, v);
    expression(lex, v);
    n++;
  }
  return n;
}


static void funcargs (LexState *lex, Expr *f) {
  FuncState *func = lex->func;
  Expr args;
  int base, nparams;
  int line = lex->linenumber;
  switch (lex->current.token) {
    case '(': {  /* funcargs -> `(' [ explist1 ] `)' */
      if (line != lex->lastline)
        luaX_syntaxerror(lex,"ambiguous syntax (function call x new statement)");
      luaX_next(lex);
      if (lex->current.token == ')')  /* arg list is empty? */
        args.kind = VVOID;
      else {
        explist1(lex, &args);
        luaK_setmultret(func, &args);
      }
      check_match(lex, ')', '(', line);
      break;
    }
    case '{': {  /* funcargs -> constructor */
      constructor(lex, &args);
      break;
    }
    case TK_STRING: {  /* funcargs -> STRING */
      codestring(lex, &args, lex->current.seminfo.ts);
      luaX_next(lex);  /* must use `seminfo' before `next' */
      break;
    }
    default: {
      luaX_syntaxerror(lex, "function arguments expected");
      return;
    }
  }
  lua_assert(f->kind == VNONRELOC);
  base = f->u.s.info;  /* base register for call */
  if (hasmultret(args.kind))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.kind != VVOID)
      luaK_exp2nextreg(func, &args);  /* close last argument */
    nparams = func->freereg - (base+1);
  }
  init_exp(f, VCALL, luaK_codeABC(func, OP_CALL, base, nparams+1, 2));
  luaK_fixline(func, line);
  func->freereg = base+1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void prefixexp (LexState *lex, Expr *v) {
  /* prefixexp -> NAME | '(' expr ')' */
  switch (lex->current.token) {
    case '(': {
      int line = lex->linenumber;
      luaX_next(lex);
      expression(lex, v);
      check_match(lex, ')', '(', line);
      luaK_dischargevars(lex->func, v);
      return;
    }
    case TK_NAME: {
      singlevar(lex, v);
      return;
    }
    default: {
      luaX_syntaxerror(lex, "unexpected symbol");
      return;
    }
  }
}


static void primaryexp (LexState *lex, Expr *v) {
  /* primaryexp ->
        prefixexp { `.' NAME | `[' exp `]' | `:' NAME funcargs | funcargs } */
  FuncState *func = lex->func;
  prefixexp(lex, v);
  for (;;) {
    switch (lex->current.token) {
      case '.': {  /* field */
        field(lex, v);
        break;
      }
      case '[': {  /* `[' exp1 `]' */
        Expr key;
        luaK_exp2anyreg(func, v);
        yindex(lex, &key);
        luaK_indexed(func, v, &key);
        break;
      }
      case ':': {  /* `:' NAME funcargs */
        Expr key;
        luaX_next(lex);
        checkname(lex, &key);
        luaK_self(func, v, &key);
        funcargs(lex, v);
        break;
      }
      case '(': case TK_STRING: case '{': {  /* funcargs */
        luaK_exp2nextreg(func, v);
        funcargs(lex, v);
        break;
      }
      default: return;
    }
  }
}


static void simpleexp (LexState *lex, Expr *v) {
  /* simpleexp -> NUMBER | STRING | NIL | true | false | ... |
                  constructor | FUNCTION body | primaryexp */
  switch (lex->current.token) {
    case TK_NUMBER: {
      init_exp(v, VKNUM, 0);
      v->u.nval = lex->current.seminfo.r;
      break;
    }
    case TK_STRING: {
      codestring(lex, v, lex->current.seminfo.ts);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *func = lex->func;
      check_condition(lex, func->proto->is_vararg,
                      "cannot use " LUA_QL("...") " outside a vararg function");
      func->proto->is_vararg &= ~VARARG_NEEDSARG;  /* don't need 'arg' */
      init_exp(v, VVARARG, luaK_codeABC(func, OP_VARARG, 0, 1, 0));
      break;
    }
    case '{': {  /* constructor */
      constructor(lex, v);
      return;
    }
    case TK_FUNCTION: {
      luaX_next(lex);
      body(lex, v, 0, lex->linenumber);
      return;
    }
    default: {
      primaryexp(lex, v);
      return;
    }
  }
  luaX_next(lex);
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '/': return OPR_DIV;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
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
static BinOpr subexpr (LexState *lex, Expr *v, unsigned int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(lex);
  uop = getunopr(lex->current.token);
  if (uop != OPR_NOUNOPR) {
    luaX_next(lex);
    subexpr(lex, v, UNARY_PRIORITY);
    luaK_prefix(lex->func, uop, v);
  }
  else simpleexp(lex, v);
  /* expand while operators have priorities higher than `limit' */
  op = getbinopr(lex->current.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    Expr v2;
    BinOpr nextop;
    luaX_next(lex);
    luaK_infix(lex->func, op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(lex, &v2, priority[op].right);
    luaK_posfix(lex->func, op, v, &v2);
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
static void expression (LexState *lex, Expr *v) {
  subexpr(lex, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static int block_follow (int token) {
  switch (token) {
    case TK_ELSE: case TK_ELSEIF: case TK_END:
    case TK_UNTIL: case TK_EOS:
      return 1;
    default: return 0;
  }
}


static void block (LexState *lex) {
  /* block -> chunk */
  FuncState *func = lex->func;
  BlockCnt bl;
  enterblock(func, &bl, 0);
  chunk(lex);
  lua_assert(bl.breaklist == NO_JUMP);
  leaveblock(func);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  Expr v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to a local variable, the local variable
** is needed in a previous assignment (to a table). If so, save original
** local value in a safe place and use this safe copy in the previous
** assignment.
*/
static void check_conflict (LexState *lex, struct LHS_assign *lh, Expr *v) {
  FuncState *func = lex->func;
  int extra = func->freereg;  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {
    if (lh->v.kind == VINDEXED) {
      if (lh->v.u.s.info == v->u.s.info) {  /* conflict? */
        conflict = 1;
        lh->v.u.s.info = extra;  /* previous assignment will use safe copy */
      }
      if (lh->v.u.s.aux == v->u.s.info) {  /* conflict? */
        conflict = 1;
        lh->v.u.s.aux = extra;  /* previous assignment will use safe copy */
      }
    }
  }
  if (conflict) {
    luaK_codeABC(func, OP_MOVE, func->freereg, v->u.s.info, 0);  /* make copy */
    luaK_reserveregs(func, 1);
  }
}


static void assignment (LexState *lex, struct LHS_assign *lh, int nvars) {
  Expr e;
  check_condition(lex, VLOCAL <= lh->v.kind && lh->v.kind <= VINDEXED,
                      "syntax error");
  if (testnext(lex, ',')) {  /* assignment -> `,' primaryexp assignment */
    struct LHS_assign nv;
    nv.prev = lh;
    primaryexp(lex, &nv.v);
    if (nv.v.kind == VLOCAL)
      check_conflict(lex, lh, &nv.v);
    luaY_checklimit(lex->func, nvars, LUAI_MAXCCALLS - lex->L->nCcalls,
                    "variables in assignment");
    assignment(lex, &nv, nvars+1);
  }
  else {  /* assignment -> `=' explist1 */
    int nexps;
    checknext(lex, '=');
    nexps = explist1(lex, &e);
    if (nexps != nvars) {
      adjust_assign(lex, nvars, nexps, &e);
      if (nexps > nvars)
        lex->func->freereg -= nexps - nvars;  /* remove extra values */
    }
    else {
      luaK_setoneret(lex->func, &e);  /* close last expression */
      luaK_storevar(lex->func, &lh->v, &e);
      return;  /* avoid default */
    }
  }
  init_exp(&e, VNONRELOC, lex->func->freereg-1);  /* default assignment */
  luaK_storevar(lex->func, &lh->v, &e);
}


static int cond (LexState *lex) {
  /* cond -> exp */
  Expr v;
  expression(lex, &v);  /* read condition */
  if (v.kind == VNIL) v.kind = VFALSE;  /* `falses' are all equal here */
  luaK_goiftrue(lex->func, &v);
  return v.f;
}


static void breakstat (LexState *lex) {
  FuncState *func = lex->func;
  BlockCnt *bl = func->bl;
  int upval = 0;
  while (bl && !bl->isbreakable) {
    upval |= bl->upval;
    bl = bl->previous;
  }
  if (!bl)
    luaX_syntaxerror(lex, "no loop to break");
  if (upval)
    luaK_codeABC(func, OP_CLOSE, bl->nactvar, 0, 0);
  luaK_concat(func, &bl->breaklist, luaK_jump(func));
}


static void whilestat (LexState *lex, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *func = lex->func;
  int whileinit;
  int condexit;
  BlockCnt bl;
  luaX_next(lex);  /* skip WHILE */
  whileinit = luaK_getlabel(func);
  condexit = cond(lex);
  enterblock(func, &bl, 1);
  checknext(lex, TK_DO);
  block(lex);
  luaK_patchlist(func, luaK_jump(func), whileinit);
  check_match(lex, TK_END, TK_WHILE, line);
  leaveblock(func);
  luaK_patchtohere(func, condexit);  /* false conditions finish the loop */
}


static void repeatstat (LexState *lex, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *func = lex->func;
  int repeat_init = luaK_getlabel(func);
  BlockCnt bl1, bl2;
  enterblock(func, &bl1, 1);  /* loop block */
  enterblock(func, &bl2, 0);  /* scope block */
  luaX_next(lex);  /* skip REPEAT */
  chunk(lex);
  check_match(lex, TK_UNTIL, TK_REPEAT, line);
  condexit = cond(lex);  /* read condition (inside scope block) */
  if (!bl2.upval) {  /* no upvalues? */
    leaveblock(func);  /* finish scope */
    luaK_patchlist(lex->func, condexit, repeat_init);  /* close the loop */
  }
  else {  /* complete semantics when there are upvalues */
    breakstat(lex);  /* if condition then break */
    luaK_patchtohere(lex->func, condexit);  /* else... */
    leaveblock(func);  /* finish scope... */
    luaK_patchlist(lex->func, luaK_jump(func), repeat_init);  /* and repeat */
  }
  leaveblock(func);  /* finish loop */
}


static int exp1 (LexState *lex) {
  Expr e;
  int k;
  expression(lex, &e);
  k = e.kind;
  luaK_exp2nextreg(lex->func, &e);
  return k;
}


static void forbody (LexState *lex, int base, int line, int nvars, int isnum) {
  /* forbody -> DO block */
  BlockCnt bl;
  FuncState *func = lex->func;
  int prep, endfor;
  adjustlocalvars(lex, 3);  /* control variables */
  checknext(lex, TK_DO);
  prep = isnum ? luaK_codeAsBx(func, OP_FORPREP, base, NO_JUMP) : luaK_jump(func);
  enterblock(func, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(lex, nvars);
  luaK_reserveregs(func, nvars);
  block(lex);
  leaveblock(func);  /* end of scope for declared variables */
  luaK_patchtohere(func, prep);
  endfor = (isnum) ? luaK_codeAsBx(func, OP_FORLOOP, base, NO_JUMP) :
                     luaK_codeABC(func, OP_TFORLOOP, base, 0, nvars);
  luaK_fixline(func, line);  /* pretend that `OP_FOR' starts the loop */
  luaK_patchlist(func, (isnum ? endfor : luaK_jump(func)), prep + 1);
}


static void fornum (LexState *lex, TString *varname, int line) {
  /* fornum -> NAME = exp1,exp1[,exp1] forbody */
  FuncState *func = lex->func;
  int base = func->freereg;
  new_localvarliteral(lex, "(for index)", 0);
  new_localvarliteral(lex, "(for limit)", 1);
  new_localvarliteral(lex, "(for step)", 2);
  new_localvar(lex, varname, 3);
  checknext(lex, '=');
  exp1(lex);  /* initial value */
  checknext(lex, ',');
  exp1(lex);  /* limit */
  if (testnext(lex, ','))
    exp1(lex);  /* optional step */
  else {  /* default step = 1 */
    luaK_codeABx(func, OP_LOADK, func->freereg, luaK_numberK(func, 1));
    luaK_reserveregs(func, 1);
  }
  forbody(lex, base, line, 1, 1);
}


static void forlist (LexState *lex, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist1 forbody */
  FuncState *func = lex->func;
  Expr e;
  int nvars = 0;
  int line;
  int base = func->freereg;
  /* create control variables */
  new_localvarliteral(lex, "(for generator)", nvars++);
  new_localvarliteral(lex, "(for state)", nvars++);
  new_localvarliteral(lex, "(for control)", nvars++);
  /* create declared variables */
  new_localvar(lex, indexname, nvars++);
  while (testnext(lex, ','))
    new_localvar(lex, str_checkname(lex), nvars++);
  checknext(lex, TK_IN);
  line = lex->linenumber;
  adjust_assign(lex, 3, explist1(lex, &e), &e);
  luaK_checkstack(func, 3);  /* extra space to call generator */
  forbody(lex, base, line, nvars - 3, 0);
}


static void forstat (LexState *lex, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *func = lex->func;
  TString *varname;
  BlockCnt bl;
  enterblock(func, &bl, 1);  /* scope for loop and control variables */
  luaX_next(lex);  /* skip `for' */
  varname = str_checkname(lex);  /* first variable name */
  switch (lex->current.token) {
    case '=': fornum(lex, varname, line); break;
    case ',': case TK_IN: forlist(lex, varname); break;
    default: luaX_syntaxerror(lex, LUA_QL("=") " or " LUA_QL("in") " expected");
  }
  check_match(lex, TK_END, TK_FOR, line);
  leaveblock(func);  /* loop scope (`break' jumps to this point) */
}


static int test_then_block (LexState *lex) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  int condexit;
  luaX_next(lex);  /* skip IF or ELSEIF */
  condexit = cond(lex);
  checknext(lex, TK_THEN);
  block(lex);  /* `then' part */
  return condexit;
}


static void ifstat (LexState *lex, int line) {
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *func = lex->func;
  int flist;
  int escapelist = NO_JUMP;
  flist = test_then_block(lex);  /* IF cond THEN block */
  while (lex->current.token == TK_ELSEIF) {
    luaK_concat(func, &escapelist, luaK_jump(func));
    luaK_patchtohere(func, flist);
    flist = test_then_block(lex);  /* ELSEIF cond THEN block */
  }
  if (lex->current.token == TK_ELSE) {
    luaK_concat(func, &escapelist, luaK_jump(func));
    luaK_patchtohere(func, flist);
    luaX_next(lex);  /* skip ELSE (after patch, for correct line info) */
    block(lex);  /* `else' part */
  }
  else
    luaK_concat(func, &escapelist, flist);
  luaK_patchtohere(func, escapelist);
  check_match(lex, TK_END, TK_IF, line);
}


static void localfunc (LexState *lex) {
  Expr v, b;
  FuncState *func = lex->func;
  new_localvar(lex, str_checkname(lex), 0);
  init_exp(&v, VLOCAL, func->freereg);
  luaK_reserveregs(func, 1);
  adjustlocalvars(lex, 1);
  body(lex, &b, 0, lex->linenumber);
  luaK_storevar(func, &v, &b);
  /* debug information will only see the variable after this point! */
  getlocvar(func, func->nactvar - 1).startpc = func->pc;
}


static void localstat (LexState *lex) {
  /* stat -> LOCAL NAME {`,' NAME} [`=' explist1] */
  int nvars = 0;
  int nexps;
  Expr e;
  do {
    new_localvar(lex, str_checkname(lex), nvars++);
  } while (testnext(lex, ','));
  if (testnext(lex, '='))
    nexps = explist1(lex, &e);
  else {
    e.kind = VVOID;
    nexps = 0;
  }
  adjust_assign(lex, nvars, nexps, &e);
  adjustlocalvars(lex, nvars);
}


static int funcname (LexState *lex, Expr *v) {
  /* funcname -> NAME {field} [`:' NAME] */
  int needself = 0;
  singlevar(lex, v);
  while (lex->current.token == '.')
    field(lex, v);
  if (lex->current.token == ':') {
    needself = 1;
    field(lex, v);
  }
  return needself;
}


static void funcstat (LexState *lex, int line) {
  /* funcstat -> FUNCTION funcname body */
  int needself;
  Expr v, b;
  luaX_next(lex);  /* skip FUNCTION */
  needself = funcname(lex, &v);
  body(lex, &b, needself, line);
  luaK_storevar(lex->func, &v, &b);
  luaK_fixline(lex->func, line);  /* definition `happens' in the first line */
}


static void exprstat (LexState *lex) {
  /* stat -> func | assignment */
  FuncState *func = lex->func;
  struct LHS_assign v;
  primaryexp(lex, &v.v);
  if (v.v.kind == VCALL)  /* stat -> func */
    SETARG_C(getcode(func, &v.v), 1);  /* call statement uses no results */
  else {  /* stat -> assignment */
    v.prev = NULL;
    assignment(lex, &v, 1);
  }
}


static void retstat (LexState *lex) {
  /* stat -> RETURN explist */
  FuncState *func = lex->func;
  Expr e;
  int first, nret;  /* registers with returned values */
  luaX_next(lex);  /* skip RETURN */
  if (block_follow(lex->current.token) || lex->current.token == ';')
    first = nret = 0;  /* return no values */
  else {
    nret = explist1(lex, &e);  /* optional return values */
    if (hasmultret(e.kind)) {
      luaK_setmultret(func, &e);
      if (e.kind == VCALL && nret == 1) {  /* tail call? */
        SET_OPCODE(getcode(func,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getcode(func,&e)) == func->nactvar);
      }
      first = func->nactvar;
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1) {  /* only one single value? */
        first = luaK_exp2anyreg(func, &e);
      }
      else {
        luaK_exp2nextreg(func, &e);  /* values must go to the `stack' */
        first = func->nactvar;  /* return all `active' values */
        lua_assert(nret == func->freereg - first);
      }
    }
  }
  luaK_ret(func, first, nret);
}


static int statement (LexState *lex) {
  int line = lex->linenumber;  /* may be needed for error messages */
  switch (lex->current.token) {
    case TK_IF: {  /* stat -> ifstat */
      ifstat(lex, line);
      return 0;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(lex, line);
      return 0;
    }
    case TK_DO: {  /* stat -> DO block END */
      luaX_next(lex);  /* skip DO */
      block(lex);
      check_match(lex, TK_END, TK_DO, line);
      return 0;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(lex, line);
      return 0;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(lex, line);
      return 0;
    }
    case TK_FUNCTION: {
      funcstat(lex, line);  /* stat -> funcstat */
      return 0;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      luaX_next(lex);  /* skip LOCAL */
      if (testnext(lex, TK_FUNCTION))  /* local function? */
        localfunc(lex);
      else
        localstat(lex);
      return 0;
    }
    case TK_RETURN: {  /* stat -> retstat */
      retstat(lex);
      return 1;  /* must be last statement */
    }
    case TK_BREAK: {  /* stat -> breakstat */
      luaX_next(lex);  /* skip BREAK */
      breakstat(lex);
      return 1;  /* must be last statement */
    }
    default: {
      exprstat(lex);
      return 0;  /* to avoid warnings */
    }
  }
}


static void chunk (LexState *lex) {
  /* chunk -> { stat [`;'] } */
  int islast = 0;
  enterlevel(lex);
  while (!islast && !block_follow(lex->current.token)) {
    islast = statement(lex);
    testnext(lex, ';');
    lua_assert(lex->func->proto->maxstacksize >= lex->func->freereg
            && lex->func->freereg >= lex->func->nactvar);
    lex->func->freereg = lex->func->nactvar;  /* free registers */
  }
  leavelevel(lex);
}

/* }====================================================================== */
