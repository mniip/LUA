/*
** $Id: Lapi.h,v 2.7 2009/11/27 15:37:59 roberto Exp $
** Auxiliary functions from LUA API
** See Copyright Notice in LUA.h
*/

#ifndef lapi_h
#define lapi_h


#include "Llimits.h"
#include "Lstate.h"

#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
				"stack overflow");}

#define adjustresults(L,nres) \
    { if ((nres) == LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }

#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


#endif
