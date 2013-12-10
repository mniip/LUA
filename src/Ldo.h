/*
** $Id: Ldo.h,v 2.20 2011/11/29 15:55:08 roberto Exp $
** Stack and Call structure of LUA
** See Copyright Notice in LUA.h
*/

#ifndef ldo_h
#define ldo_h


#include "Lobject.h"
#include "Lstate.h"
#include "Lzio.h"


#define LUAD_checkstack(L,n)	if (L->stack_last - L->top <= (n)) \
				    LUAD_growstack(L, n); else condmovestack(L);


#define incr_top(L) {L->top++; LUAD_checkstack(L,0);}

#define savestack(L,p)		((char *)(p) - (char *)L->stack)
#define restorestack(L,n)	((TValue *)((char *)L->stack + (n)))


/* type of protected functions, to be ran by `runprotected' */
typedef void (*Pfunc) (LUA_State *L, void *ud);

LUAI_FUNC int LUAD_protectedparser (LUA_State *L, ZIO *z, const char *name,
                                                  const char *mode);
LUAI_FUNC void LUAD_hook (LUA_State *L, int event, int line);
LUAI_FUNC int LUAD_precall (LUA_State *L, StkId func, int nresults);
LUAI_FUNC void LUAD_call (LUA_State *L, StkId func, int nResults,
                                        int allowyield);
LUAI_FUNC int LUAD_pcall (LUA_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
LUAI_FUNC int LUAD_poscall (LUA_State *L, StkId firstResult);
LUAI_FUNC void LUAD_reallocstack (LUA_State *L, int newsize);
LUAI_FUNC void LUAD_growstack (LUA_State *L, int n);
LUAI_FUNC void LUAD_shrinkstack (LUA_State *L);

LUAI_FUNC l_noret LUAD_throw (LUA_State *L, int errcode);
LUAI_FUNC int LUAD_rawrunprotected (LUA_State *L, Pfunc f, void *ud);

#endif

