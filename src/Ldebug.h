/*
** $Id: Ldebug.h,v 2.7 2011/10/07 20:45:19 roberto Exp $
** Auxiliary functions from Debug Interface module
** See Copyright Notice in LUA.h
*/

#ifndef ldebug_h
#define ldebug_h


#include "Lstate.h"


#define pcRel(pc, p)	(cast(int, (pc) - (p)->code) - 1)

#define getfuncline(f,pc)	(((f)->lineinfo) ? (f)->lineinfo[pc] : 0)

#define resethookcount(L)	(L->hookcount = L->basehookcount)

/* Active LUA function (given call info) */
#define ci_func(ci)		(clLvalue((ci)->func))


LUAI_FUNC l_noret LUAG_typeerror (LUA_State *L, const TValue *o,
                                                const char *opname);
LUAI_FUNC l_noret LUAG_concaterror (LUA_State *L, StkId p1, StkId p2);
LUAI_FUNC l_noret LUAG_aritherror (LUA_State *L, const TValue *p1,
                                                 const TValue *p2);
LUAI_FUNC l_noret LUAG_ordererror (LUA_State *L, const TValue *p1,
                                                 const TValue *p2);
LUAI_FUNC l_noret LUAG_runerror (LUA_State *L, const char *fmt, ...);
LUAI_FUNC l_noret LUAG_errormsg (LUA_State *L);

#endif
