/*
** $Id: lfunc.c,v 2.30 2012/10/03 12:36:46 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in LUA.h
*/


#include <stddef.h>

#define lfunc_c
#define LUA_CORE

#include "LUA.h"

#include "Lfunc.h"
#include "Lgc.h"
#include "Lmem.h"
#include "Lobject.h"
#include "Lstate.h"



Closure *LUAF_newCclosure (LUA_State *L, int n) {
  Closure *c = &LUAC_newobj(L, LUA_TCCL, sizeCclosure(n), NULL, 0)->cl;
  c->c.nupvalues = cast_byte(n);
  return c;
}


Closure *LUAF_newLclosure (LUA_State *L, int n) {
  Closure *c = &LUAC_newobj(L, LUA_TLCL, sizeLclosure(n), NULL, 0)->cl;
  c->l.p = NULL;
  c->l.nupvalues = cast_byte(n);
  while (n--) c->l.upvals[n] = NULL;
  return c;
}


UpVal *LUAF_newupval (LUA_State *L) {
  UpVal *uv = &LUAC_newobj(L, LUA_TUPVAL, sizeof(UpVal), NULL, 0)->uv;
  uv->v = &uv->u.value;
  setnilvalue(uv->v);
  return uv;
}


UpVal *LUAF_findupval (LUA_State *L, StkId level) {
  global_State *g = G(L);
  GCObject **pp = &L->openupval;
  UpVal *p;
  UpVal *uv;
  while (*pp != NULL && (p = gco2uv(*pp))->v >= level) {
    GCObject *o = obj2gco(p);
    LUA_assert(p->v != &p->u.value);
    LUA_assert(!isold(o) || isold(obj2gco(L)));
    if (p->v == level) {  /* found a corresponding upvalue? */
      if (isdead(g, o))  /* is it dead? */
        changewhite(o);  /* resurrect it */
      return p;
    }
    pp = &p->next;
  }
  /* not found: create a new one */
  uv = &LUAC_newobj(L, LUA_TUPVAL, sizeof(UpVal), pp, 0)->uv;
  uv->v = level;  /* current value lives in the stack */
  uv->u.l.prev = &g->uvhead;  /* double link it in `uvhead' list */
  uv->u.l.next = g->uvhead.u.l.next;
  uv->u.l.next->u.l.prev = uv;
  g->uvhead.u.l.next = uv;
  LUA_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  return uv;
}


static void unlinkupval (UpVal *uv) {
  LUA_assert(uv->u.l.next->u.l.prev == uv && uv->u.l.prev->u.l.next == uv);
  uv->u.l.next->u.l.prev = uv->u.l.prev;  /* remove from `uvhead' list */
  uv->u.l.prev->u.l.next = uv->u.l.next;
}


void LUAF_freeupval (LUA_State *L, UpVal *uv) {
  if (uv->v != &uv->u.value)  /* is it open? */
    unlinkupval(uv);  /* remove from open list */
  LUAM_free(L, uv);  /* free upvalue */
}


void LUAF_close (LUA_State *L, StkId level) {
  UpVal *uv;
  global_State *g = G(L);
  while (L->openupval != NULL && (uv = gco2uv(L->openupval))->v >= level) {
    GCObject *o = obj2gco(uv);
    LUA_assert(!isblack(o) && uv->v != &uv->u.value);
    L->openupval = uv->next;  /* remove from `open' list */
    if (isdead(g, o))
      LUAF_freeupval(L, uv);  /* free upvalue */
    else {
      unlinkupval(uv);  /* remove upvalue from 'uvhead' list */
      setobj(L, &uv->u.value, uv->v);  /* move value to upvalue slot */
      uv->v = &uv->u.value;  /* now current value lives here */
      gch(o)->next = g->allgc;  /* link upvalue into 'allgc' list */
      g->allgc = o;
      LUAC_checkupvalcolor(g, uv);
    }
  }
}


Proto *LUAF_newproto (LUA_State *L) {
  Proto *f = &LUAC_newobj(L, LUA_TPROTO, sizeof(Proto), NULL, 0)->p;
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->cache = NULL;
  f->sizecode = 0;
  f->lineinfo = NULL;
  f->sizelineinfo = 0;
  f->upvalues = NULL;
  f->sizeupvalues = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->locvars = NULL;
  f->sizelocvars = 0;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}


void LUAF_freeproto (LUA_State *L, Proto *f) {
  LUAM_freearray(L, f->code, f->sizecode);
  LUAM_freearray(L, f->p, f->sizep);
  LUAM_freearray(L, f->k, f->sizek);
  LUAM_freearray(L, f->lineinfo, f->sizelineinfo);
  LUAM_freearray(L, f->locvars, f->sizelocvars);
  LUAM_freearray(L, f->upvalues, f->sizeupvalues);
  LUAM_free(L, f);
}


/*
** Look for n-th local variable at line `line' in function `func'.
** Returns NULL if not found.
*/
const char *LUAF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;  /* not found */
}

