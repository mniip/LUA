/*
** $Id: lvm.c,v 2.155 2013/03/16 21:10:18 roberto Exp $
** LUA virtual machine
** See Copyright Notice in LUA.h
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lvm_c
#define LUA_CORE

#include "LUA.h"

#include "Ldebug.h"
#include "Ldo.h"
#include "Lfunc.h"
#include "Lgc.h"
#include "Lobject.h"
#include "Lopcodes.h"
#include "Lstate.h"
#include "Lstring.h"
#include "Ltable.h"
#include "Ltm.h"
#include "Lvm.h"



/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	100


const TValue *LUAV_tonumber (const TValue *obj, TValue *n) {
  LUA_Number num;
  if (ttisnumber(obj)) return obj;
  if (ttisstring(obj) && LUAO_str2d(svalue(obj), tsvalue(obj)->len, &num)) {
    setnvalue(n, num);
    return n;
  }
  else
    return NULL;
}


int LUAV_tostring (LUA_State *L, StkId obj) {
  if (!ttisnumber(obj))
    return 0;
  else {
    char s[LUAI_MAXNUMBER2STR];
    LUA_Number n = nvalue(obj);
    int l = LUA_number2str(s, n);
    setsvalue2s(L, obj, LUAS_newlstr(L, s, l));
    return 1;
  }
}


static void traceexec (LUA_State *L) {
  CallInfo *ci = L->ci;
  lu_byte mask = L->hookmask;
  int counthook = ((mask & LUA_MASKCOUNT) && L->hookcount == 0);
  if (counthook)
    resethookcount(L);  /* reset count */
  if (ci->callstatus & CIST_HOOKYIELD) {  /* called hook last time? */
    ci->callstatus &= ~CIST_HOOKYIELD;  /* erase mark */
    return;  /* do not call hook again (VM yielded, so it did not move) */
  }
  if (counthook)
    LUAD_hook(L, LUA_HOOKCOUNT, -1);  /* call count hook */
  if (mask & LUA_MASKLINE) {
    Proto *p = ci_func(ci)->p;
    int npc = pcRel(ci->u.l.savedpc, p);
    int newline = getfuncline(p, npc);
    if (npc == 0 ||  /* call linehook when enter a new function, */
        ci->u.l.savedpc <= L->oldpc ||  /* when jump back (loop), or when */
        newline != getfuncline(p, pcRel(L->oldpc, p)))  /* enter a new line */
      LUAD_hook(L, LUA_HOOKLINE, newline);  /* call line hook */
  }
  L->oldpc = ci->u.l.savedpc;
  if (L->status == LUA_YIELD) {  /* did hook yield? */
    if (counthook)
      L->hookcount = 1;  /* undo decrement to zero */
    ci->u.l.savedpc--;  /* undo increment (resume will increment it again) */
    ci->callstatus |= CIST_HOOKYIELD;  /* mark that it yielded */
    ci->func = L->top - 1;  /* protect stack below results */
    LUAD_throw(L, LUA_YIELD);
  }
}


static void callTM (LUA_State *L, const TValue *f, const TValue *p1,
                    const TValue *p2, TValue *p3, int hasres) {
  ptrdiff_t result = savestack(L, p3);
  setobj2s(L, L->top++, f);  /* push function */
  setobj2s(L, L->top++, p1);  /* 1st argument */
  setobj2s(L, L->top++, p2);  /* 2nd argument */
  if (!hasres)  /* no result? 'p3' is third argument */
    setobj2s(L, L->top++, p3);  /* 3rd argument */
  /* metamethod may yield only when called from LUA code */
  LUAD_call(L, L->top - (4 - hasres), hasres, isLUA(L->ci));
  if (hasres) {  /* if has result, move it to its place */
    p3 = restorestack(L, result);
    setobjs2s(L, p3, --L->top);
  }
}


void LUAV_gettable (LUA_State *L, const TValue *t, TValue *key, StkId val) {
  int loop;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;
    if (ttistable(t)) {  /* `t' is a table? */
      Table *h = hvalue(t);
      const TValue *res = LUAH_get(h, key); /* do a primitive get */
      if (!ttisnil(res) ||  /* result is not nil? */
          (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL) { /* or no TM? */
        setobj2s(L, val, res);
        return;
      }
      /* else will try the tag method */
    }
    else if (ttisnil(tm = LUAT_gettmbyobj(L, t, TM_INDEX)))
      LUAG_typeerror(L, t, "index");
    if (ttisfunction(tm)) {
      callTM(L, tm, t, key, val, 1);
      return;
    }
    t = tm;  /* else repeat with 'tm' */
  }
  LUAG_runerror(L, "loop in gettable");
}


void LUAV_settable (LUA_State *L, const TValue *t, TValue *key, StkId val) {
  int loop;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;
    if (ttistable(t)) {  /* `t' is a table? */
      Table *h = hvalue(t);
      TValue *oldval = cast(TValue *, LUAH_get(h, key));
      /* if previous value is not nil, there must be a previous entry
         in the table; moreover, a metamethod has no relevance */
      if (!ttisnil(oldval) ||
         /* previous value is nil; must check the metamethod */
         ((tm = fasttm(L, h->metatable, TM_NEWINDEX)) == NULL &&
         /* no metamethod; is there a previous entry in the table? */
         (oldval != LUAO_nilobject ||
         /* no previous entry; must create one. (The next test is
            always true; we only need the assignment.) */
         (oldval = LUAH_newkey(L, h, key), 1)))) {
        /* no metamethod and (now) there is an entry with given key */
        setobj2t(L, oldval, val);  /* assign new value to that entry */
        invalidateTMcache(h);
        LUAC_barrierback(L, obj2gco(h), val);
        return;
      }
      /* else will try the metamethod */
    }
    else  /* not a table; check metamethod */
      if (ttisnil(tm = LUAT_gettmbyobj(L, t, TM_NEWINDEX)))
        LUAG_typeerror(L, t, "index");
    /* there is a metamethod */
    if (ttisfunction(tm)) {
      callTM(L, tm, t, key, val, 0);
      return;
    }
    t = tm;  /* else repeat with 'tm' */
  }
  LUAG_runerror(L, "loop in settable");
}


static int call_binTM (LUA_State *L, const TValue *p1, const TValue *p2,
                       StkId res, TMS event) {
  const TValue *tm = LUAT_gettmbyobj(L, p1, event);  /* try first operand */
  if (ttisnil(tm))
    tm = LUAT_gettmbyobj(L, p2, event);  /* try second operand */
  if (ttisnil(tm)) return 0;
  callTM(L, tm, p1, p2, res, 1);
  return 1;
}


static const TValue *get_equalTM (LUA_State *L, Table *mt1, Table *mt2,
                                  TMS event) {
  const TValue *tm1 = fasttm(L, mt1, event);
  const TValue *tm2;
  if (tm1 == NULL) return NULL;  /* no metamethod */
  if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
  tm2 = fasttm(L, mt2, event);
  if (tm2 == NULL) return NULL;  /* no metamethod */
  if (LUAV_rawequalobj(tm1, tm2))  /* same metamethods? */
    return tm1;
  return NULL;
}


static int call_orderTM (LUA_State *L, const TValue *p1, const TValue *p2,
                         TMS event) {
  if (!call_binTM(L, p1, p2, L->top, event))
    return -1;  /* no metamethod */
  else
    return !l_isfalse(L->top);
}


static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = getstr(ls);
  size_t ll = ls->tsv.len;
  const char *r = getstr(rs);
  size_t lr = rs->tsv.len;
  for (;;) {
    int temp = strcoll(l, r);
    if (temp != 0) return temp;
    else {  /* strings are equal up to a `\0' */
      size_t len = strlen(l);  /* index of first `\0' in both strings */
      if (len == lr)  /* r is finished? */
        return (len == ll) ? 0 : 1;
      else if (len == ll)  /* l is finished? */
        return -1;  /* l is smaller than r (because r is not finished) */
      /* both strings longer than `len'; go on comparing (after the `\0') */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}


int LUAV_lessthan (LUA_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttisnumber(l) && ttisnumber(r))
    return LUAi_numlt(L, nvalue(l), nvalue(r));
  else if (ttisstring(l) && ttisstring(r))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) < 0;
  else if ((res = call_orderTM(L, l, r, TM_LT)) < 0)
    LUAG_ordererror(L, l, r);
  return res;
}


int LUAV_lessequal (LUA_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttisnumber(l) && ttisnumber(r))
    return LUAi_numle(L, nvalue(l), nvalue(r));
  else if (ttisstring(l) && ttisstring(r))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) <= 0;
  else if ((res = call_orderTM(L, l, r, TM_LE)) >= 0)  /* first try `le' */
    return res;
  else if ((res = call_orderTM(L, r, l, TM_LT)) < 0)  /* else try `lt' */
    LUAG_ordererror(L, l, r);
  return !res;
}


/*
** equality of LUA values. L == NULL means raw equality (no metamethods)
*/
int LUAV_equalobj_ (LUA_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  LUA_assert(ttisequal(t1, t2));
  switch (ttype(t1)) {
    case LUA_TNIL: return 1;
    case LUA_TNUMBER: return LUAi_numeq(nvalue(t1), nvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TLCF: return fvalue(t1) == fvalue(t2);
    case LUA_TSHRSTR: return eqshrstr(rawtsvalue(t1), rawtsvalue(t2));
    case LUA_TLNGSTR: return LUAS_eqlngstr(rawtsvalue(t1), rawtsvalue(t2));
    case LUA_TUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = get_equalTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    case LUA_TTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = get_equalTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    default:
      LUA_assert(iscollectable(t1));
      return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL) return 0;  /* no TM? */
  callTM(L, tm, t1, t2, L->top, 1);  /* call TM */
  return !l_isfalse(L->top);
}


void LUAV_concat (LUA_State *L, int total) {
  LUA_assert(total >= 2);
  do {
    StkId top = L->top;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(ttisstring(top-2) || ttisnumber(top-2)) || !tostring(L, top-1)) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT))
        LUAG_concaterror(L, top-2, top-1);
    }
    else if (tsvalue(top-1)->len == 0)  /* second operand is empty? */
      (void)tostring(L, top - 2);  /* result is first operand */
    else if (ttisstring(top-2) && tsvalue(top-2)->len == 0) {
      setobjs2s(L, top - 2, top - 1);  /* result is second op. */
    }
    else {
      /* at least two non-empty string values; get as many as possible */
      size_t tl = tsvalue(top-1)->len;
      char *buffer;
      int i;
      /* collect total length */
      for (i = 1; i < total && tostring(L, top-i-1); i++) {
        size_t l = tsvalue(top-i-1)->len;
        if (l >= (MAX_SIZET/sizeof(char)) - tl)
          LUAG_runerror(L, "string length overflow");
        tl += l;
      }
      buffer = LUAZ_openspace(L, &G(L)->buff, tl);
      tl = 0;
      n = i;
      do {  /* concat all strings */
        size_t l = tsvalue(top-i)->len;
        memcpy(buffer+tl, svalue(top-i), l * sizeof(char));
        tl += l;
      } while (--i > 0);
      setsvalue2s(L, top-n, LUAS_newlstr(L, buffer, tl));
    }
    total -= n-1;  /* got 'n' strings to create 1 new */
    L->top -= n-1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


void LUAV_objlen (LUA_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttypenv(rb)) {
    case LUA_TTABLE: {
      Table *h = hvalue(rb);
      tm = fasttm(L, h->metatable, TM_LEN);
      if (tm) break;  /* metamethod? break switch to call it */
      setnvalue(ra, cast_num(LUAH_getn(h)));  /* else primitive len */
      return;
    }
    case LUA_TSTRING: {
      setnvalue(ra, cast_num(tsvalue(rb)->len));
      return;
    }
    default: {  /* try metamethod */
      tm = LUAT_gettmbyobj(L, rb, TM_LEN);
      if (ttisnil(tm))  /* no metamethod? */
        LUAG_typeerror(L, rb, "get length of");
      break;
    }
  }
  callTM(L, tm, rb, rb, ra, 1);
}


void LUAV_arith (LUA_State *L, StkId ra, const TValue *rb,
                 const TValue *rc, TMS op) {
  TValue tempb, tempc;
  const TValue *b, *c;
  if ((b = LUAV_tonumber(rb, &tempb)) != NULL &&
      (c = LUAV_tonumber(rc, &tempc)) != NULL) {
    LUA_Number res = LUAO_arith(op - TM_ADD + LUA_OPADD, nvalue(b), nvalue(c));
    setnvalue(ra, res);
  }
  else if (!call_binTM(L, rb, rc, ra, op))
    LUAG_aritherror(L, rb, rc);
}


/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
*/
static Closure *getcached (Proto *p, UpVal **encup, StkId base) {
  Closure *c = p->cache;
  if (c != NULL) {  /* is there a cached closure? */
    int nup = p->sizeupvalues;
    Upvaldesc *uv = p->upvalues;
    int i;
    for (i = 0; i < nup; i++) {  /* check whether it has right upvalues */
      TValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
      if (c->l.upvals[i]->v != v)
        return NULL;  /* wrong upvalue; cannot reuse closure */
    }
  }
  return c;  /* return cached closure (or NULL if no cached closure) */
}


/*
** create a new LUA closure, push it in the stack, and initialize
** its upvalues. Note that the call to 'LUAC_barrierproto' must come
** before the assignment to 'p->cache', as the function needs the
** original value of that field.
*/
static void pushclosure (LUA_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  Closure *ncl = LUAF_newLclosure(L, nup);
  ncl->l.p = p;
  setclLvalue(L, ra, ncl);  /* anchor new closure in stack */
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].instack)  /* upvalue refers to local variable? */
      ncl->l.upvals[i] = LUAF_findupval(L, base + uv[i].idx);
    else  /* get upvalue from enclosing function */
      ncl->l.upvals[i] = encup[uv[i].idx];
  }
  LUAC_barrierproto(L, p, ncl);
  p->cache = ncl;  /* save it on cache for reuse */
}


/*
** finish execution of an opcode interrupted by an yield
*/
void LUAV_finishOp (LUA_State *L) {
  CallInfo *ci = L->ci;
  StkId base = ci->u.l.base;
  Instruction inst = *(ci->u.l.savedpc - 1);  /* interrupted instruction */
  OpCode op = GET_OPCODE(inst);
  switch (op) {  /* finish its execution */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_POW: case OP_UNM: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_SELF: {
      setobjs2s(L, base + GETARG_A(inst), --L->top);
      break;
    }
    case OP_LE: case OP_LT: case OP_EQ: {
      int res = !l_isfalse(L->top - 1);
      L->top--;
      /* metamethod should not be called when operand is K */
      LUA_assert(!ISK(GETARG_B(inst)));
      if (op == OP_LE &&  /* "<=" using "<" instead? */
          ttisnil(LUAT_gettmbyobj(L, base + GETARG_B(inst), TM_LE)))
        res = !res;  /* invert result */
      LUA_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_JMP);
      if (res != GETARG_A(inst))  /* condition failed? */
        ci->u.l.savedpc++;  /* skip jump instruction */
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top - 1;  /* top when 'call_binTM' was called */
      int b = GETARG_B(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + b));  /* yet to concatenate */
      setobj2s(L, top - 2, top);  /* put TM result in proper position */
      if (total > 1) {  /* are there elements to concat? */
        L->top = top - 1;  /* top is one after last element (at top-2) */
        LUAV_concat(L, total);  /* concat them (may yield again) */
      }
      /* move final result to final position */
      setobj2s(L, ci->u.l.base + GETARG_A(inst), L->top - 1);
      L->top = ci->top;  /* restore top */
      break;
    }
    case OP_TFORCALL: {
      LUA_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_TFORLOOP);
      L->top = ci->top;  /* correct top */
      break;
    }
    case OP_CALL: {
      if (GETARG_C(inst) - 1 >= 0)  /* nresults >= 0? */
        L->top = ci->top;  /* adjust results */
      break;
    }
    case OP_TAILCALL: case OP_SETTABUP: case OP_SETTABLE:
      break;
    default: LUA_assert(0);
  }
}



/*
** some macros for common tasks in `LUAV_execute'
*/

#if !defined LUAi_runtimecheck
#define LUAi_runtimecheck(L, c)		/* void */
#endif


#define RA(i)	(base+GETARG_A(i))
/* to be used after possible stack reallocation */
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))
#define KBx(i)  \
  (k + (GETARG_Bx(i) != 0 ? GETARG_Bx(i) - 1 : GETARG_Ax(*ci->u.l.savedpc++)))


/* execute a jump instruction */
#define dojump(ci,i,e) \
  { int a = GETARG_A(i); \
    if (a > 0) LUAF_close(L, ci->u.l.base + a - 1); \
    ci->u.l.savedpc += GETARG_sBx(i) + e; }

/* for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ i = *ci->u.l.savedpc; dojump(ci, i, 1); }


#define Protect(x)	{ {x;}; base = ci->u.l.base; }

#define checkGC(L,c)  \
  Protect( LUAC_condGC(L,{L->top = (c);  /* limit of live values */ \
                          LUAC_step(L); \
                          L->top = ci->top;})  /* restore top */ \
           LUAi_threadyield(L); )


#define arith_op(op,tm) { \
        TValue *rb = RKB(i); \
        TValue *rc = RKC(i); \
        if (ttisnumber(rb) && ttisnumber(rc)) { \
          LUA_Number nb = nvalue(rb), nc = nvalue(rc); \
          setnvalue(ra, op(L, nb, nc)); \
        } \
        else { Protect(LUAV_arith(L, ra, rb, rc, tm)); } }


#define vmdispatch(o)	switch(o)
#define vmcase(l,b)	case l: {b}  break;
#define vmcasenb(l,b)	case l: {b}		/* nb = no break */

void LUAV_execute (LUA_State *L) {
  CallInfo *ci = L->ci;
  LClosure *cl;
  TValue *k;
  StkId base;
 newframe:  /* reentry point when frame changes (call/return) */
  LUA_assert(ci == L->ci);
  cl = clLvalue(ci->func);
  k = cl->p->k;
  base = ci->u.l.base;
  /* main loop of interpreter */
  for (;;) {
    Instruction i = *(ci->u.l.savedpc++);
    StkId ra;
    if ((L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&
        (--L->hookcount == 0 || L->hookmask & LUA_MASKLINE)) {
      Protect(traceexec(L));
    }
    /* WARNING: several calls may realloc the stack and invalidate `ra' */
    ra = RA(i);
    LUA_assert(base == ci->u.l.base);
    LUA_assert(base <= L->top && L->top < L->stack + L->stacksize);
    vmdispatch (GET_OPCODE(i)) {
      vmcase(OP_MOVE,
        setobjs2s(L, ra, RB(i));
      )
      vmcase(OP_LOADK,
        TValue *rb = k + GETARG_Bx(i);
        setobj2s(L, ra, rb);
      )
      vmcase(OP_LOADKX,
        TValue *rb;
        LUA_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_EXTRAARG);
        rb = k + GETARG_Ax(*ci->u.l.savedpc++);
        setobj2s(L, ra, rb);
      )
      vmcase(OP_LOADBOOL,
        setbvalue(ra, GETARG_B(i));
        if (GETARG_C(i)) ci->u.l.savedpc++;  /* skip next instruction (if C) */
      )
      vmcase(OP_LOADNIL,
        int b = GETARG_B(i);
        do {
          setnilvalue(ra++);
        } while (b--);
      )
      vmcase(OP_GETUPVAL,
        int b = GETARG_B(i);
        setobj2s(L, ra, cl->upvals[b]->v);
      )
      vmcase(OP_GETTABUP,
        int b = GETARG_B(i);
        Protect(LUAV_gettable(L, cl->upvals[b]->v, RKC(i), ra));
      )
      vmcase(OP_GETTABLE,
        Protect(LUAV_gettable(L, RB(i), RKC(i), ra));
      )
      vmcase(OP_SETTABUP,
        int a = GETARG_A(i);
        Protect(LUAV_settable(L, cl->upvals[a]->v, RKB(i), RKC(i)));
      )
      vmcase(OP_SETUPVAL,
        UpVal *uv = cl->upvals[GETARG_B(i)];
        setobj(L, uv->v, ra);
        LUAC_barrier(L, uv, ra);
      )
      vmcase(OP_SETTABLE,
        Protect(LUAV_settable(L, ra, RKB(i), RKC(i)));
      )
      vmcase(OP_NEWTABLE,
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        Table *t = LUAH_new(L);
        sethvalue(L, ra, t);
        if (b != 0 || c != 0)
          LUAH_resize(L, t, LUAO_fb2int(b), LUAO_fb2int(c));
        checkGC(L, ra + 1);
      )
      vmcase(OP_SELF,
        StkId rb = RB(i);
        setobjs2s(L, ra+1, rb);
        Protect(LUAV_gettable(L, rb, RKC(i), ra));
      )
      vmcase(OP_ADD,
        arith_op(LUAi_numadd, TM_ADD);
      )
      vmcase(OP_SUB,
        arith_op(LUAi_numsub, TM_SUB);
      )
      vmcase(OP_MUL,
        arith_op(LUAi_nummul, TM_MUL);
      )
      vmcase(OP_DIV,
        arith_op(LUAi_numdiv, TM_DIV);
      )
      vmcase(OP_MOD,
        arith_op(LUAi_nummod, TM_MOD);
      )
      vmcase(OP_POW,
        arith_op(LUAi_numpow, TM_POW);
      )
      vmcase(OP_UNM,
        TValue *rb = RB(i);
        if (ttisnumber(rb)) {
          LUA_Number nb = nvalue(rb);
          setnvalue(ra, LUAi_numunm(L, nb));
        }
        else {
          Protect(LUAV_arith(L, ra, rb, rb, TM_UNM));
        }
      )
      vmcase(OP_NOT,
        TValue *rb = RB(i);
        int res = l_isfalse(rb);  /* next assignment may change this value */
        setbvalue(ra, res);
      )
      vmcase(OP_LEN,
        Protect(LUAV_objlen(L, ra, RB(i)));
      )
      vmcase(OP_CONCAT,
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        StkId rb;
        L->top = base + c + 1;  /* mark the end of concat operands */
        Protect(LUAV_concat(L, c - b + 1));
        ra = RA(i);  /* 'LUAv_concat' may invoke TMs and move the stack */
        rb = b + base;
        setobjs2s(L, ra, rb);
        checkGC(L, (ra >= rb ? ra + 1 : rb));
        L->top = ci->top;  /* restore top */
      )
      vmcase(OP_JMP,
        dojump(ci, i, 0);
      )
      vmcase(OP_EQ,
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        Protect(
          if (cast_int(equalobj(L, rb, rc)) != GETARG_A(i))
            ci->u.l.savedpc++;
          else
            donextjump(ci);
        )
      )
      vmcase(OP_LT,
        Protect(
          if (LUAV_lessthan(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->u.l.savedpc++;
          else
            donextjump(ci);
        )
      )
      vmcase(OP_LE,
        Protect(
          if (LUAV_lessequal(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->u.l.savedpc++;
          else
            donextjump(ci);
        )
      )
      vmcase(OP_TEST,
        if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra))
            ci->u.l.savedpc++;
          else
          donextjump(ci);
      )
      vmcase(OP_TESTSET,
        TValue *rb = RB(i);
        if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb))
          ci->u.l.savedpc++;
        else {
          setobjs2s(L, ra, rb);
          donextjump(ci);
        }
      )
      vmcase(OP_CALL,
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        if (LUAD_precall(L, ra, nresults)) {  /* C function? */
          if (nresults >= 0) L->top = ci->top;  /* adjust results */
          base = ci->u.l.base;
        }
        else {  /* LUA function */
          ci = L->ci;
          ci->callstatus |= CIST_REENTRY;
          goto newframe;  /* restart LUAV_execute over new LUA function */
        }
      )
      vmcase(OP_TAILCALL,
        int b = GETARG_B(i);
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        LUA_assert(GETARG_C(i) - 1 == LUA_MULTRET);
        if (LUAD_precall(L, ra, LUA_MULTRET))  /* C function? */
          base = ci->u.l.base;
        else {
          /* tail call: put called frame (n) in place of caller one (o) */
          CallInfo *nci = L->ci;  /* called frame */
          CallInfo *oci = nci->previous;  /* caller frame */
          StkId nfunc = nci->func;  /* called function */
          StkId ofunc = oci->func;  /* caller function */
          /* last stack slot filled by 'precall' */
          StkId lim = nci->u.l.base + getproto(nfunc)->numparams;
          int aux;
          /* close all upvalues from previous call */
          if (cl->p->sizep > 0) LUAF_close(L, oci->u.l.base);
          /* move new frame into old one */
          for (aux = 0; nfunc + aux < lim; aux++)
            setobjs2s(L, ofunc + aux, nfunc + aux);
          oci->u.l.base = ofunc + (nci->u.l.base - nfunc);  /* correct base */
          oci->top = L->top = ofunc + (L->top - nfunc);  /* correct top */
          oci->u.l.savedpc = nci->u.l.savedpc;
          oci->callstatus |= CIST_TAIL;  /* function was tail called */
          ci = L->ci = oci;  /* remove new frame */
          LUA_assert(L->top == oci->u.l.base + getproto(ofunc)->maxstacksize);
          goto newframe;  /* restart LUAV_execute over new LUA function */
        }
      )
      vmcasenb(OP_RETURN,
        int b = GETARG_B(i);
        if (b != 0) L->top = ra+b-1;
        if (cl->p->sizep > 0) LUAF_close(L, base);
        b = LUAD_poscall(L, ra);
        if (!(ci->callstatus & CIST_REENTRY))  /* 'ci' still the called one */
          return;  /* external invocation: return */
        else {  /* invocation via reentry: continue execution */
          ci = L->ci;
          if (b) L->top = ci->top;
          LUA_assert(isLUA(ci));
          LUA_assert(GET_OPCODE(*((ci)->u.l.savedpc - 1)) == OP_CALL);
          goto newframe;  /* restart LUAV_execute over new LUA function */
        }
      )
      vmcase(OP_FORLOOP,
        LUA_Number step = nvalue(ra+2);
        LUA_Number idx = LUAi_numadd(L, nvalue(ra), step); /* increment index */
        LUA_Number limit = nvalue(ra+1);
        if (LUAi_numlt(L, 0, step) ? LUAi_numle(L, idx, limit)
                                   : LUAi_numle(L, limit, idx)) {
          ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
          setnvalue(ra, idx);  /* update internal index... */
          setnvalue(ra+3, idx);  /* ...and external index */
        }
      )
      vmcase(OP_FORPREP,
        const TValue *init = ra;
        const TValue *plimit = ra+1;
        const TValue *pstep = ra+2;
        if (!tonumber(init, ra))
          LUAG_runerror(L, LUA_QL("FOR") " initial value must be a number");
        else if (!tonumber(plimit, ra+1))
          LUAG_runerror(L, LUA_QL("FOR") " limit must be a number");
        else if (!tonumber(pstep, ra+2))
          LUAG_runerror(L, LUA_QL("FOR") " step must be a number");
        setnvalue(ra, LUAi_numsub(L, nvalue(ra), nvalue(pstep)));
        ci->u.l.savedpc += GETARG_sBx(i);
      )
      vmcasenb(OP_TFORCALL,
        StkId cb = ra + 3;  /* call base */
        setobjs2s(L, cb+2, ra+2);
        setobjs2s(L, cb+1, ra+1);
        setobjs2s(L, cb, ra);
        L->top = cb + 3;  /* func. + 2 args (state and index) */
        Protect(LUAD_call(L, cb, GETARG_C(i), 1));
        L->top = ci->top;
        i = *(ci->u.l.savedpc++);  /* go to next instruction */
        ra = RA(i);
        LUA_assert(GET_OPCODE(i) == OP_TFORLOOP);
        goto l_tforloop;
      )
      vmcase(OP_TFORLOOP,
        l_tforloop:
        if (!ttisnil(ra + 1)) {  /* continue loop? */
          setobjs2s(L, ra, ra + 1);  /* save control variable */
           ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
        }
      )
      vmcase(OP_SETLIST,
        int n = GETARG_B(i);
        int c = GETARG_C(i);
        int last;
        Table *h;
        if (n == 0) n = cast_int(L->top - ra) - 1;
        if (c == 0) {
          LUA_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_EXTRAARG);
          c = GETARG_Ax(*ci->u.l.savedpc++);
        }
        LUAi_runtimecheck(L, ttistable(ra));
        h = hvalue(ra);
        last = ((c-1)*LFIELDS_PER_FLUSH) + n - 2;
        if (last > h->sizearray)  /* needs more space? */
          LUAH_resizearray(L, h, last);  /* pre-allocate it at once */
        for (; n > 0; n--) {
          TValue *val = ra+n;
          LUAH_setint(L, h, last--, val);
          LUAC_barrierback(L, obj2gco(h), val);
        }
        L->top = ci->top;  /* correct top (in case of previous open call) */
      )
      vmcase(OP_CLOSURE,
        Proto *p = cl->p->p[GETARG_Bx(i)];
        Closure *ncl = getcached(p, cl->upvals, base);  /* cached closure */
        if (ncl == NULL)  /* no match? */
          pushclosure(L, p, cl->upvals, base, ra);  /* create a new one */
        else
          setclLvalue(L, ra, ncl);  /* push cashed closure */
        checkGC(L, ra + 1);
      )
      vmcase(OP_VARARG,
        int b = GETARG_B(i) - 1;
        int j;
        int n = cast_int(base - ci->func) - cl->p->numparams - 1;
        if (b < 0) {  /* B == 0? */
          b = n;  /* get all var. arguments */
          Protect(LUAD_checkstack(L, n));
          ra = RA(i);  /* previous call may change the stack */
          L->top = ra + n;
        }
        for (j = 0; j < b; j++) {
          if (j < n) {
            setobjs2s(L, ra + j, base - n + j);
          }
          else {
            setnilvalue(ra + j);
          }
        }
      )
      vmcase(OP_EXTRAARG,
        LUA_assert(0);
      )
    }
  }
}

