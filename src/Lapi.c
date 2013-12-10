/*
** $Id: lapi.c,v 2.171 2013/03/16 21:10:18 roberto Exp $
** LUA API
** See Copyright Notice in LUA.h
*/


#include <stdarg.h>
#include <string.h>

#define lapi_c
#define LUA_CORE

#include "LUA.h"

#include "Lapi.h"
#include "Ldebug.h"
#include "Ldo.h"
#include "Lfunc.h"
#include "Lgc.h"
#include "Lmem.h"
#include "Lobject.h"
#include "Lstate.h"
#include "Lstring.h"
#include "Ltable.h"
#include "Ltm.h"
#include "Lundump.h"
#include "Lvm.h"



const char LUA_ident[] =
  "$LUAVersion: " LUA_COPYRIGHT " $"
  "$LUAAuthors: " LUA_AUTHORS " $";


/* value at a non-valid index */
#define NONVALIDVALUE		cast(TValue *, LUAO_nilobject)

/* corresponding test */
#define isvalid(o)	((o) != LUAO_nilobject)

/* test for pseudo index */
#define ispseudo(i)		((i) <= LUA_REGISTRYINDEX)

/* test for valid but not pseudo index */
#define isstackindex(i, o)	(isvalid(o) && !ispseudo(i))

#define api_checkvalidindex(L, o)  api_check(L, isvalid(o), "invalid index")

#define api_checkstackindex(L, i, o)  \
	api_check(L, isstackindex(i, o), "index not in the stack")


static TValue *index2addr (LUA_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    TValue *o = ci->func + idx;
    api_check(L, idx <= ci->top - (ci->func + 1), "unacceptable index");
    if (o >= L->top) return NONVALIDVALUE;
    else return o;
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    return L->top + idx;
  }
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {  /* upvalues */
    idx = LUA_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttislcf(ci->func))  /* light C function? */
      return NONVALIDVALUE;  /* it has no upvalues */
    else {
      CClosure *func = clCvalue(ci->func);
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1] : NONVALIDVALUE;
    }
  }
}


/*
** to be called by 'LUA_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
static void growstack (LUA_State *L, void *ud) {
  int size = *(int *)ud;
  LUAD_growstack(L, size);
}


LUA_API int LUA_checkstack (LUA_State *L, int size) {
  int res;
  CallInfo *ci = L->ci;
  LUA_lock(L);
  if (L->stack_last - L->top > size)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack */
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - size)  /* can grow without overflow? */
      res = 0;  /* no */
    else  /* try to grow stack */
      res = (LUAD_rawrunprotected(L, &growstack, &size) == LUA_OK);
  }
  if (res && ci->top < L->top + size)
    ci->top = L->top + size;  /* adjust frame top */
  LUA_unlock(L);
  return res;
}


LUA_API void LUA_xmove (LUA_State *from, LUA_State *to, int n) {
  int i;
  if (from == to) return;
  LUA_lock(to);
  api_checknelems(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->ci->top - to->top >= n, "not enough elements to move");
  from->top -= n;
  for (i = 0; i < n; i++) {
    setobj2s(to, to->top++, from->top + i);
  }
  LUA_unlock(to);
}


LUA_API LUA_CFunction LUA_atpanic (LUA_State *L, LUA_CFunction panicf) {
  LUA_CFunction old;
  LUA_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  LUA_unlock(L);
  return old;
}


LUA_API const LUA_Number *LUA_version (LUA_State *L) {
  static const LUA_Number version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
LUA_API int LUA_absindex (LUA_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func + idx);
}


LUA_API int LUA_gettop (LUA_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}


LUA_API void LUA_settop (LUA_State *L, int idx) {
  StkId func = L->ci->func;
  LUA_lock(L);
  if (idx >= 0) {
    api_check(L, idx <= L->stack_last - (func + 1), "new top too large");
    while (L->top < (func + 1) + idx)
      setnilvalue(L->top++);
    L->top = (func + 1) + idx;
  }
  else {
    api_check(L, -(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* `subtract' index (index is negative) */
  }
  LUA_unlock(L);
}


LUA_API void LUA_remove (LUA_State *L, int idx) {
  StkId p;
  LUA_lock(L);
  p = index2addr(L, idx);
  api_checkstackindex(L, idx, p);
  while (++p < L->top) setobjs2s(L, p-1, p);
  L->top--;
  LUA_unlock(L);
}


LUA_API void LUA_insert (LUA_State *L, int idx) {
  StkId p;
  StkId q;
  LUA_lock(L);
  p = index2addr(L, idx);
  api_checkstackindex(L, idx, p);
  for (q = L->top; q > p; q--)  /* use L->top as a temporary */
    setobjs2s(L, q, q - 1);
  setobjs2s(L, p, L->top);
  LUA_unlock(L);
}


static void moveto (LUA_State *L, TValue *fr, int idx) {
  TValue *to = index2addr(L, idx);
  api_checkvalidindex(L, to);
  setobj(L, to, fr);
  if (idx < LUA_REGISTRYINDEX)  /* function upvalue? */
    LUAC_barrier(L, clCvalue(L->ci->func), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
}


LUA_API void LUA_replace (LUA_State *L, int idx) {
  LUA_lock(L);
  api_checknelems(L, 1);
  moveto(L, L->top - 1, idx);
  L->top--;
  LUA_unlock(L);
}


LUA_API void LUA_copy (LUA_State *L, int fromidx, int toidx) {
  TValue *fr;
  LUA_lock(L);
  fr = index2addr(L, fromidx);
  moveto(L, fr, toidx);
  LUA_unlock(L);
}


LUA_API void LUA_pushvalue (LUA_State *L, int idx) {
  LUA_lock(L);
  setobj2s(L, L->top, index2addr(L, idx));
  api_incr_top(L);
  LUA_unlock(L);
}



/*
** access functions (stack -> C)
*/


LUA_API int LUA_type (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttypenv(o) : LUA_TNONE);
}


LUA_API const char *LUA_typename (LUA_State *L, int t) {
  UNUSED(L);
  return ttypename(t);
}


LUA_API int LUA_iscfunction (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


LUA_API int LUA_isnumber (LUA_State *L, int idx) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}


LUA_API int LUA_isstring (LUA_State *L, int idx) {
  int t = LUA_type(L, idx);
  return (t == LUA_TSTRING || t == LUA_TNUMBER);
}


LUA_API int LUA_isuserdata (LUA_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisuserdata(o) || ttislightuserdata(o));
}


LUA_API int LUA_rawequal (LUA_State *L, int index1, int index2) {
  StkId o1 = index2addr(L, index1);
  StkId o2 = index2addr(L, index2);
  return (isvalid(o1) && isvalid(o2)) ? LUAV_rawequalobj(o1, o2) : 0;
}


LUA_API void LUA_arith (LUA_State *L, int op) {
  StkId o1;  /* 1st operand */
  StkId o2;  /* 2nd operand */
  LUA_lock(L);
  if (op != LUA_OPUNM) /* all other operations expect two operands */
    api_checknelems(L, 2);
  else {  /* for unary minus, add fake 2nd operand */
    api_checknelems(L, 1);
    setobjs2s(L, L->top, L->top - 1);
    L->top++;
  }
  o1 = L->top - 2;
  o2 = L->top - 1;
  if (ttisnumber(o1) && ttisnumber(o2)) {
    setnvalue(o1, LUAO_arith(op, nvalue(o1), nvalue(o2)));
  }
  else
    LUAV_arith(L, o1, o1, o2, cast(TMS, op - LUA_OPADD + TM_ADD));
  L->top--;
  LUA_unlock(L);
}


LUA_API int LUA_compare (LUA_State *L, int index1, int index2, int op) {
  StkId o1, o2;
  int i = 0;
  LUA_lock(L);  /* may call tag method */
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case LUA_OPEQ: i = equalobj(L, o1, o2); break;
      case LUA_OPLT: i = LUAV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = LUAV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  LUA_unlock(L);
  return i;
}


LUA_API LUA_Number LUA_tonumberx (LUA_State *L, int idx, int *isnum) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    if (isnum) *isnum = 1;
    return nvalue(o);
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


LUA_API LUA_Integer LUA_tointegerx (LUA_State *L, int idx, int *isnum) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    LUA_Integer res;
    LUA_Number num = nvalue(o);
    LUA_number2integer(res, num);
    if (isnum) *isnum = 1;
    return res;
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


LUA_API LUA_Unsigned LUA_tounsignedx (LUA_State *L, int idx, int *isnum) {
  TValue n;
  const TValue *o = index2addr(L, idx);
  if (tonumber(o, &n)) {
    LUA_Unsigned res;
    LUA_Number num = nvalue(o);
    LUA_number2unsigned(res, num);
    if (isnum) *isnum = 1;
    return res;
  }
  else {
    if (isnum) *isnum = 0;
    return 0;
  }
}


LUA_API int LUA_toboolean (LUA_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}


LUA_API const char *LUA_tolstring (LUA_State *L, int idx, size_t *len) {
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
    LUA_lock(L);  /* `LUAV_tostring' may create a new string */
    if (!LUAV_tostring(L, o)) {  /* conversion failed? */
      if (len != NULL) *len = 0;
      LUA_unlock(L);
      return NULL;
    }
    LUAC_checkGC(L);
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
    LUA_unlock(L);
  }
  if (len != NULL) *len = tsvalue(o)->len;
  return svalue(o);
}


LUA_API size_t LUA_rawlen (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttypenv(o)) {
    case LUA_TSTRING: return tsvalue(o)->len;
    case LUA_TUSERDATA: return uvalue(o)->len;
    case LUA_TTABLE: return LUAH_getn(hvalue(o));
    default: return 0;
  }
}


LUA_API LUA_CFunction LUA_tocfunction (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}


LUA_API void *LUA_touserdata (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttypenv(o)) {
    case LUA_TUSERDATA: return (rawuvalue(o) + 1);
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


LUA_API LUA_State *LUA_tothread (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


LUA_API const void *LUA_topointer (LUA_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case LUA_TTABLE: return hvalue(o);
    case LUA_TLCL: return clLvalue(o);
    case LUA_TCCL: return clCvalue(o);
    case LUA_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    case LUA_TTHREAD: return thvalue(o);
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
      return LUA_touserdata(L, idx);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


LUA_API void LUA_pushnil (LUA_State *L) {
  LUA_lock(L);
  setnilvalue(L->top);
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_pushnumber (LUA_State *L, LUA_Number n) {
  LUA_lock(L);
  setnvalue(L->top, n);
  LUAi_checknum(L, L->top,
    LUAG_runerror(L, "C API - attempt to push a signaling NaN"));
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_pushinteger (LUA_State *L, LUA_Integer n) {
  LUA_lock(L);
  setnvalue(L->top, cast_num(n));
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_pushunsigned (LUA_State *L, LUA_Unsigned u) {
  LUA_Number n;
  LUA_lock(L);
  n = LUA_unsigned2number(u);
  setnvalue(L->top, n);
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API const char *LUA_pushlstring (LUA_State *L, const char *s, size_t len) {
  TString *ts;
  LUA_lock(L);
  LUAC_checkGC(L);
  ts = LUAS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  LUA_unlock(L);
  return getstr(ts);
}


LUA_API const char *LUA_pushstring (LUA_State *L, const char *s) {
  if (s == NULL) {
    LUA_pushnil(L);
    return NULL;
  }
  else {
    TString *ts;
    LUA_lock(L);
    LUAC_checkGC(L);
    ts = LUAS_new(L, s);
    setsvalue2s(L, L->top, ts);
    api_incr_top(L);
    LUA_unlock(L);
    return getstr(ts);
  }
}


LUA_API const char *LUA_pushvfstring (LUA_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  LUA_lock(L);
  LUAC_checkGC(L);
  ret = LUAO_pushvfstring(L, fmt, argp);
  LUA_unlock(L);
  return ret;
}


LUA_API const char *LUA_pushfstring (LUA_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  LUA_lock(L);
  LUAC_checkGC(L);
  va_start(argp, fmt);
  ret = LUAO_pushvfstring(L, fmt, argp);
  va_end(argp);
  LUA_unlock(L);
  return ret;
}


LUA_API void LUA_pushcclosure (LUA_State *L, LUA_CFunction fn, int n) {
  LUA_lock(L);
  if (n == 0) {
    setfvalue(L->top, fn);
  }
  else {
    Closure *cl;
    api_checknelems(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    LUAC_checkGC(L);
    cl = LUAF_newCclosure(L, n);
    cl->c.f = fn;
    L->top -= n;
    while (n--)
      setobj2n(L, &cl->c.upvalue[n], L->top + n);
    setclCvalue(L, L->top, cl);
  }
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_pushboolean (LUA_State *L, int b) {
  LUA_lock(L);
  setbvalue(L->top, (b != 0));  /* ensure that true is 1 */
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_pushlightuserdata (LUA_State *L, void *p) {
  LUA_lock(L);
  setpvalue(L->top, p);
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API int LUA_pushthread (LUA_State *L) {
  LUA_lock(L);
  setthvalue(L, L->top, L);
  api_incr_top(L);
  LUA_unlock(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (LUA -> stack)
*/


LUA_API void LUA_getglobal (LUA_State *L, const char *var) {
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  LUA_lock(L);
  gt = LUAH_getint(reg, LUA_RIDX_GLOBALS);
  setsvalue2s(L, L->top++, LUAS_new(L, var));
  LUAV_gettable(L, gt, L->top - 1, L->top - 1);
  LUA_unlock(L);
}


LUA_API void LUA_gettable (LUA_State *L, int idx) {
  StkId t;
  LUA_lock(L);
  t = index2addr(L, idx);
  LUAV_gettable(L, t, L->top - 1, L->top - 1);
  LUA_unlock(L);
}


LUA_API void LUA_getfield (LUA_State *L, int idx, const char *k) {
  StkId t;
  LUA_lock(L);
  t = index2addr(L, idx);
  setsvalue2s(L, L->top, LUAS_new(L, k));
  api_incr_top(L);
  LUAV_gettable(L, t, L->top - 1, L->top - 1);
  LUA_unlock(L);
}


LUA_API void LUA_rawget (LUA_State *L, int idx) {
  StkId t;
  LUA_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2s(L, L->top - 1, LUAH_get(hvalue(t), L->top - 1));
  LUA_unlock(L);
}


LUA_API void LUA_rawgeti (LUA_State *L, int idx, int n) {
  StkId t;
  LUA_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2s(L, L->top, LUAH_getint(hvalue(t), n));
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_rawgetp (LUA_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  LUA_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj2s(L, L->top, LUAH_get(hvalue(t), &k));
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API void LUA_createtable (LUA_State *L, int narray, int nrec) {
  Table *t;
  LUA_lock(L);
  LUAC_checkGC(L);
  t = LUAH_new(L);
  sethvalue(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    LUAH_resize(L, t, narray, nrec);
  LUA_unlock(L);
}


LUA_API int LUA_getmetatable (LUA_State *L, int objindex) {
  const TValue *obj;
  Table *mt = NULL;
  int res;
  LUA_lock(L);
  obj = index2addr(L, objindex);
  switch (ttypenv(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttypenv(obj)];
      break;
  }
  if (mt == NULL)
    res = 0;
  else {
    sethvalue(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  LUA_unlock(L);
  return res;
}


LUA_API void LUA_getuservalue (LUA_State *L, int idx) {
  StkId o;
  LUA_lock(L);
  o = index2addr(L, idx);
  api_check(L, ttisuserdata(o), "userdata expected");
  if (uvalue(o)->env) {
    sethvalue(L, L->top, uvalue(o)->env);
  } else
    setnilvalue(L->top);
  api_incr_top(L);
  LUA_unlock(L);
}


/*
** set functions (stack -> LUA)
*/


LUA_API void LUA_setglobal (LUA_State *L, const char *var) {
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  LUA_lock(L);
  api_checknelems(L, 1);
  gt = LUAH_getint(reg, LUA_RIDX_GLOBALS);
  setsvalue2s(L, L->top++, LUAS_new(L, var));
  LUAV_settable(L, gt, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  LUA_unlock(L);
}


LUA_API void LUA_settable (LUA_State *L, int idx) {
  StkId t;
  LUA_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  LUAV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
  LUA_unlock(L);
}


LUA_API void LUA_setfield (LUA_State *L, int idx, const char *k) {
  StkId t;
  LUA_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  setsvalue2s(L, L->top++, LUAS_new(L, k));
  LUAV_settable(L, t, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  LUA_unlock(L);
}


LUA_API void LUA_rawset (LUA_State *L, int idx) {
  StkId t;
  LUA_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2t(L, LUAH_set(L, hvalue(t), L->top-2), L->top-1);
  invalidateTMcache(hvalue(t));
  LUAC_barrierback(L, gcvalue(t), L->top-1);
  L->top -= 2;
  LUA_unlock(L);
}


LUA_API void LUA_rawseti (LUA_State *L, int idx, int n) {
  StkId t;
  LUA_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  LUAH_setint(L, hvalue(t), n, L->top - 1);
  LUAC_barrierback(L, gcvalue(t), L->top-1);
  L->top--;
  LUA_unlock(L);
}


LUA_API void LUA_rawsetp (LUA_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  LUA_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj2t(L, LUAH_set(L, hvalue(t), &k), L->top - 1);
  LUAC_barrierback(L, gcvalue(t), L->top - 1);
  L->top--;
  LUA_unlock(L);
}


LUA_API int LUA_setmetatable (LUA_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  LUA_lock(L);
  api_checknelems(L, 1);
  obj = index2addr(L, objindex);
  if (ttisnil(L->top - 1))
    mt = NULL;
  else {
    api_check(L, ttistable(L->top - 1), "table expected");
    mt = hvalue(L->top - 1);
  }
  switch (ttypenv(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        LUAC_objbarrierback(L, gcvalue(obj), mt);
        LUAC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        LUAC_objbarrier(L, rawuvalue(obj), mt);
        LUAC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttypenv(obj)] = mt;
      break;
    }
  }
  L->top--;
  LUA_unlock(L);
  return 1;
}


LUA_API void LUA_setuservalue (LUA_State *L, int idx) {
  StkId o;
  LUA_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttisuserdata(o), "userdata expected");
  if (ttisnil(L->top - 1))
    uvalue(o)->env = NULL;
  else {
    api_check(L, ttistable(L->top - 1), "table expected");
    uvalue(o)->env = hvalue(L->top - 1);
    LUAC_objbarrier(L, gcvalue(o), hvalue(L->top - 1));
  }
  L->top--;
  LUA_unlock(L);
}


/*
** `load' and `call' functions (run LUA code)
*/


#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")


LUA_API int LUA_getctx (LUA_State *L, int *ctx) {
  if (L->ci->callstatus & CIST_YIELDED) {
    if (ctx) *ctx = L->ci->u.c.ctx;
    return L->ci->u.c.status;
  }
  else return LUA_OK;
}


LUA_API void LUA_callk (LUA_State *L, int nargs, int nresults, int ctx,
                        LUA_CFunction k) {
  StkId func;
  LUA_lock(L);
  api_check(L, k == NULL || !isLUA(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  if (k != NULL && L->nny == 0) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    LUAD_call(L, func, nresults, 1);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    LUAD_call(L, func, nresults, 0);  /* just do the call */
  adjustresults(L, nresults);
  LUA_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to `f_call' */
  StkId func;
  int nresults;
};


static void f_call (LUA_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  LUAD_call(L, c->func, c->nresults, 0);
}



LUA_API int LUA_pcallk (LUA_State *L, int nargs, int nresults, int errfunc,
                        int ctx, LUA_CFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  LUA_lock(L);
  api_check(L, k == NULL || !isLUA(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);
    api_checkstackindex(L, errfunc, o);
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);  /* function to be called */
  if (k == NULL || L->nny > 0) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = LUAD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->extra = savestack(L, c.func);
    ci->u.c.old_allowhook = L->allowhook;
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    /* mark that function may do error recovery */
    ci->callstatus |= CIST_YPCALL;
    LUAD_call(L, c.func, nresults, 1);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = LUA_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  LUA_unlock(L);
  return status;
}


LUA_API int LUA_load (LUA_State *L, LUA_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  LUA_lock(L);
  if (!chunkname) chunkname = "?";
  LUAZ_init(L, &z, reader, data);
  status = LUAD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    LClosure *f = clLvalue(L->top - 1);  /* get newly created function */
    if (f->nupvalues == 1) {  /* does it have one upvalue? */
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = LUAH_getint(reg, LUA_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      LUAC_barrier(L, f->upvals[0], gt);
    }
  }
  LUA_unlock(L);
  return status;
}


LUA_API int LUA_dump (LUA_State *L, LUA_Writer writer, void *data) {
  int status;
  TValue *o;
  LUA_lock(L);
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = LUAU_dump(L, getproto(o), writer, data, 0);
  else
    status = 1;
  LUA_unlock(L);
  return status;
}


LUA_API int LUA_status (LUA_State *L) {
  return L->status;
}


/*
** Garbage-collection function
*/

LUA_API int LUA_gc (LUA_State *L, int what, int data) {
  int res = 0;
  global_State *g;
  LUA_lock(L);
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      LUAE_setdebt(g, 0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      LUAC_fullgc(L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      if (g->gckind == KGC_GEN) {  /* generational mode? */
        res = (g->GCestimate == 0);  /* true if it will do major collection */
        LUAC_forcestep(L);  /* do a single step */
      }
      else {
       lu_mem debt = cast(lu_mem, data) * 1024 - GCSTEPSIZE;
       if (g->gcrunning)
         debt += g->GCdebt;  /* include current debt */
       LUAE_setdebt(g, debt);
       LUAC_forcestep(L);
       if (g->gcstate == GCSpause)  /* end of cycle? */
         res = 1;  /* signal it */
      }
      break;
    }
    case LUA_GCSETPAUSE: {
      res = g->gcpause;
      g->gcpause = data;
      break;
    }
    case LUA_GCSETMAJORINC: {
      res = g->gcmajorinc;
      g->gcmajorinc = data;
      break;
    }
    case LUA_GCSETSTEPMUL: {
      res = g->gcstepmul;
      g->gcstepmul = data;
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->gcrunning;
      break;
    }
    case LUA_GCGEN: {  /* change collector to generational mode */
      LUAC_changemode(L, KGC_GEN);
      break;
    }
    case LUA_GCINC: {  /* change collector to incremental mode */
      LUAC_changemode(L, KGC_NORMAL);
      break;
    }
    default: res = -1;  /* invalid option */
  }
  LUA_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


LUA_API int LUA_error (LUA_State *L) {
  LUA_lock(L);
  api_checknelems(L, 1);
  LUAG_errormsg(L);
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}


LUA_API int LUA_next (LUA_State *L, int idx) {
  StkId t;
  int more;
  LUA_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  more = LUAH_next(L, hvalue(t), L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  LUA_unlock(L);
  return more;
}


LUA_API void LUA_concat (LUA_State *L, int n) {
  LUA_lock(L);
  api_checknelems(L, n);
  if (n >= 2) {
    LUAC_checkGC(L);
    LUAV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    setsvalue2s(L, L->top, LUAS_newlstr(L, "", 0));
    api_incr_top(L);
  }
  /* else n == 1; nothing to do */
  LUA_unlock(L);
}


LUA_API void LUA_len (LUA_State *L, int idx) {
  StkId t;
  LUA_lock(L);
  t = index2addr(L, idx);
  LUAV_objlen(L, L->top, t);
  api_incr_top(L);
  LUA_unlock(L);
}


LUA_API LUA_Alloc LUA_getallocf (LUA_State *L, void **ud) {
  LUA_Alloc f;
  LUA_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  LUA_unlock(L);
  return f;
}


LUA_API void LUA_setallocf (LUA_State *L, LUA_Alloc f, void *ud) {
  LUA_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  LUA_unlock(L);
}


LUA_API void *LUA_newuserdata (LUA_State *L, size_t size) {
  Udata *u;
  LUA_lock(L);
  LUAC_checkGC(L);
  u = LUAS_newudata(L, size, NULL);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  LUA_unlock(L);
  return u + 1;
}



static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttype(fi)) {
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->upvalue[n-1];
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case LUA_TLCL: {  /* LUA closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
      *val = f->upvals[n-1]->v;
      if (owner) *owner = obj2gco(f->upvals[n - 1]);
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}


LUA_API const char *LUA_getupvalue (LUA_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  LUA_lock(L);
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL);
  if (name) {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  LUA_unlock(L);
  return name;
}


LUA_API const char *LUA_setupvalue (LUA_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  GCObject *owner = NULL;  /* to avoid warnings */
  StkId fi;
  LUA_lock(L);
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->top--;
    setobj(L, val, L->top);
    LUAC_barrier(L, owner, L->top);
  }
  LUA_unlock(L);
  return name;
}


static UpVal **getupvalref (LUA_State *L, int fidx, int n, LClosure **pf) {
  LClosure *f;
  StkId fi = index2addr(L, fidx);
  api_check(L, ttisLclosure(fi), "LUA function expected");
  f = clLvalue(fi);
  api_check(L, (1 <= n && n <= f->p->sizeupvalues), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->upvals[n - 1];  /* get its upvalue pointer */
}


LUA_API void *LUA_upvalueid (LUA_State *L, int fidx, int n) {
  StkId fi = index2addr(L, fidx);
  switch (ttype(fi)) {
    case LUA_TLCL: {  /* LUA closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      api_check(L, 1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->upvalue[n - 1];
    }
    default: {
      api_check(L, 0, "closure expected");
      return NULL;
    }
  }
}


LUA_API void LUA_upvaluejoin (LUA_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  *up1 = *up2;
  LUAC_objbarrier(L, f1, *up2);
}

