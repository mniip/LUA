/*
** $Id: Lstring.h,v 1.49 2012/02/01 21:57:15 roberto Exp $
** String table (keep all strings handled by LUA)
** See Copyright Notice in LUA.h
*/

#ifndef lstring_h
#define lstring_h

#include "Lgc.h"
#include "Lobject.h"
#include "Lstate.h"


#define sizestring(s)	(sizeof(union TString)+((s)->len+1)*sizeof(char))

#define sizeudata(u)	(sizeof(union Udata)+(u)->len)

#define LUAS_newliteral(L, s)	(LUAS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))

#define LUAS_fix(s)	l_setbit((s)->tsv.marked, FIXEDBIT)


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tsv.tt == LUA_TSHRSTR && (s)->tsv.extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tsv.tt == LUA_TSHRSTR, (a) == (b))


LUAI_FUNC unsigned int LUAS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC int LUAS_eqlngstr (TString *a, TString *b);
LUAI_FUNC int LUAS_eqstr (TString *a, TString *b);
LUAI_FUNC void LUAS_resize (LUA_State *L, int newsize);
LUAI_FUNC Udata *LUAS_newudata (LUA_State *L, size_t s, Table *e);
LUAI_FUNC TString *LUAS_newlstr (LUA_State *L, const char *str, size_t l);
LUAI_FUNC TString *LUAS_new (LUA_State *L, const char *str);


#endif
