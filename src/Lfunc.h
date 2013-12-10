/*
** $Id: Lfunc.h,v 2.8 2012/05/08 13:53:33 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in LUA.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "Lobject.h"


#define sizeCclosure(n)	(cast(int, sizeof(CClosure)) + \
                         cast(int, sizeof(TValue)*((n)-1)))

#define sizeLclosure(n)	(cast(int, sizeof(LClosure)) + \
                         cast(int, sizeof(TValue *)*((n)-1)))


LUAI_FUNC Proto *LUAF_newproto (LUA_State *L);
LUAI_FUNC Closure *LUAF_newCclosure (LUA_State *L, int nelems);
LUAI_FUNC Closure *LUAF_newLclosure (LUA_State *L, int nelems);
LUAI_FUNC UpVal *LUAF_newupval (LUA_State *L);
LUAI_FUNC UpVal *LUAF_findupval (LUA_State *L, StkId level);
LUAI_FUNC void LUAF_close (LUA_State *L, StkId level);
LUAI_FUNC void LUAF_freeproto (LUA_State *L, Proto *f);
LUAI_FUNC void LUAF_freeupval (LUA_State *L, UpVal *uv);
LUAI_FUNC const char *LUAF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
