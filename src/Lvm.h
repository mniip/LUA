/*
** $Id: Lvm.h,v 2.18 2013/01/08 14:06:55 roberto Exp $
** LUA virtual machine
** See Copyright Notice in LUA.h
*/

#ifndef lvm_h
#define lvm_h


#include "Ldo.h"
#include "Lobject.h"
#include "Ltm.h"


#define tostring(L,o) (ttisstring(o) || (LUAV_tostring(L, o)))

#define tonumber(o,n)	(ttisnumber(o) || (((o) = LUAV_tonumber(o,n)) != NULL))

#define equalobj(L,o1,o2)  (ttisequal(o1, o2) && LUAV_equalobj_(L, o1, o2))

#define LUAV_rawequalobj(o1,o2)		equalobj(NULL,o1,o2)


/* not to called directly */
LUAI_FUNC int LUAV_equalobj_ (LUA_State *L, const TValue *t1, const TValue *t2);


LUAI_FUNC int LUAV_lessthan (LUA_State *L, const TValue *l, const TValue *r);
LUAI_FUNC int LUAV_lessequal (LUA_State *L, const TValue *l, const TValue *r);
LUAI_FUNC const TValue *LUAV_tonumber (const TValue *obj, TValue *n);
LUAI_FUNC int LUAV_tostring (LUA_State *L, StkId obj);
LUAI_FUNC void LUAV_gettable (LUA_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void LUAV_settable (LUA_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void LUAV_finishOp (LUA_State *L);
LUAI_FUNC void LUAV_execute (LUA_State *L);
LUAI_FUNC void LUAV_concat (LUA_State *L, int total);
LUAI_FUNC void LUAV_arith (LUA_State *L, StkId ra, const TValue *rb,
                           const TValue *rc, TMS op);
LUAI_FUNC void LUAV_objlen (LUA_State *L, StkId ra, const TValue *rb);

#endif
