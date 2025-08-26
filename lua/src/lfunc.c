/*
** $Id: lfunc.c,v 2.12.1.2 2007/12/28 14:58:43 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/


#include <stddef.h>

#define lfunc_c
#define LUA_CORE

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



Closure *luaF_newCclosure (lua_State *L, int nelems, Table *e) {
  Closure *c = cast(Closure *, luaM_malloc(L, sizeCclosure(nelems)));
  luaC_link(L, obj2gco(c), LUA_TFUNCTION);
  c->c.isC = 1;
  c->c.env = e;
  c->c.nupvalues = cast_byte(nelems);
  return c;
}


Closure *luaF_newLclosure (lua_State *L, int nelems, Table *e) {
  Closure *c = cast(Closure *, luaM_malloc(L, sizeLclosure(nelems)));
  luaC_link(L, obj2gco(c), LUA_TFUNCTION);
  c->l.isC = 0;
  c->l.env = e;
  c->l.nupvalues = cast_byte(nelems);
  while (nelems--) c->l.upvals[nelems] = NULL;
  return c;
}


UpVal *luaF_newupval (lua_State *L) {
  UpVal *uv = luaM_new(L, UpVal);
  luaC_link(L, obj2gco(uv), LUA_TUPVAL);
  uv->v = &uv->u.value;
  setnilvalue(uv->v);
  return uv;
}


UpVal *luaF_findupval (lua_State *L, StkId level) {
  global_State *g = G(L);
  GCObject **pp = &L->openupval;
  UpVal *p;
  UpVal *uv;
  while (*pp != NULL /* && (p = ngcotouv(*pp))->v >= level */) {
    p = ngcotouv(*pp);
    /* We are below the stack index of 'level'? */
    if (p->v < level) {
      break;
    }

    lua_assert(p->v != &p->u.value); /* ensure upvalue is not yet closed */
    if (p->v == level) {  /* found a corresponding upvalue? */
      if (isdead(g, obj2gco(p)))  /* is it dead? */
        changewhite(obj2gco(p));  /* ressurect it */
      return p;
    }
    pp = &p->next;
  }
  uv = luaM_new(L, UpVal);  /* not found: create a new one */
  uv->tt = LUA_TUPVAL;
  uv->marked = luaC_white(g);
  uv->v = level;  /* current value lives in the stack */
  uv->next = *pp;  /* chain it in the proper position */
  *pp = obj2gco(uv);

  uv->u.l.prev = &g->uvhead;  /* double link it in `uvhead' list */
  uv->u.l.next = g->uvhead.u.l.next;

  /* link ourselves to our new neighbors */
  uv->u.l.next->u.l.prev = uv;
  g->uvhead.u.l.next = uv;
  lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  return uv;
}


static void unlinkupval (UpVal *uv) {
  lua_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  uv->u.l.next->u.l.prev = uv->u.l.prev;  /* remove from `uvhead' list */
  uv->u.l.prev->u.l.next = uv->u.l.next;
}


void luaF_freeupval (lua_State *L, UpVal *uv) {
  if (uv->v != &uv->u.value)  /* is it open? */
    unlinkupval(uv);  /* remove from open list */
  luaM_free(L, uv);  /* free upvalue */
}


void luaF_close (lua_State *L, StkId level) {
  UpVal *uv;
  global_State *g = G(L);
  while (L->openupval != NULL && (uv = ngcotouv(L->openupval))->v >= level) {
    GCObject *o = obj2gco(uv);
    lua_assert(!isblack(o) && uv->v != &uv->u.value);
    L->openupval = uv->next;  /* remove from `open' list */
    if (isdead(g, o))
      luaF_freeupval(L, uv);  /* free upvalue */
    else {
      unlinkupval(uv);
      setobj(L, &uv->u.value, uv->v);
      uv->v = &uv->u.value;  /* now current value lives here */
      luaC_linkupval(L, uv);  /* link upvalue into `gcroot' list */
    }
  }
}


Proto *luaF_newproto (lua_State *L) {
  Proto *f = luaM_new(L, Proto);
  luaC_link(L, obj2gco(f), LUA_TPROTO);
  f->constants = NULL;
  f->size_constants = 0;
  f->children = NULL;
  f->size_children = 0;
  f->code = NULL;
  f->size_code = 0;
  f->size_lineinfo = 0;
  f->size_upvalues = 0;
  f->nups = 0;
  f->upvalues = NULL;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->lineinfo = NULL;
  f->size_locvars = 0;
  f->locvars = NULL;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}


void luaF_freeproto (lua_State *L, Proto *f) {
  luaM_freearray(L, f->code, f->size_code, Instruction);
  luaM_freearray(L, f->children, f->size_children, Proto *);
  luaM_freearray(L, f->constants, f->size_constants, TValue);
  luaM_freearray(L, f->lineinfo, f->size_lineinfo, int);
  luaM_freearray(L, f->locvars, f->size_locvars, struct LocVar);
  luaM_freearray(L, f->upvalues, f->size_upvalues, TString *);
  luaM_free(L, f);
}


void luaF_freeclosure (lua_State *L, Closure *c) {
  int size = (c->c.isC) ? sizeCclosure(c->c.nupvalues) :
                          sizeLclosure(c->l.nupvalues);
  luaM_freemem(L, c, size);
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns NULL if not found.
*/
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  int counter = local_number;
  /* Are we in range of the culprit instruction index `pc`? */
  for (i = 0; i < f->size_locvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active at this point? */
      counter--;
      if (counter == 0) { /* number of iterations matches what we're after? */
        return getstr(f->locvars[i].varname);
      }
    }
  }
  return NULL;  /* not found */
}

