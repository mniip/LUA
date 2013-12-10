/*
** $Id: Ltable.h,v 2.16 2011/08/17 20:26:47 roberto Exp $
** LUA tables (hash)
** See Copyright Notice in LUA.h
*/

#ifndef ltable_h
#define ltable_h

#include "Lobject.h"


#define gnode(t,i)	(&(t)->node[i])
#define gkey(n)		(&(n)->i_key.tvk)
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->i_key.nk.next)

#define invalidateTMcache(t)	((t)->flags = 0)


LUAI_FUNC const TValue *LUAH_getint (Table *t, int key);
LUAI_FUNC void LUAH_setint (LUA_State *L, Table *t, int key, TValue *value);
LUAI_FUNC const TValue *LUAH_getstr (Table *t, TString *key);
LUAI_FUNC const TValue *LUAH_get (Table *t, const TValue *key);
LUAI_FUNC TValue *LUAH_newkey (LUA_State *L, Table *t, const TValue *key);
LUAI_FUNC TValue *LUAH_set (LUA_State *L, Table *t, const TValue *key);
LUAI_FUNC Table *LUAH_new (LUA_State *L);
LUAI_FUNC void LUAH_resize (LUA_State *L, Table *t, int nasize, int nhsize);
LUAI_FUNC void LUAH_resizearray (LUA_State *L, Table *t, int nasize);
LUAI_FUNC void LUAH_free (LUA_State *L, Table *t);
LUAI_FUNC int LUAH_next (LUA_State *L, Table *t, StkId key);
LUAI_FUNC int LUAH_getn (Table *t);


#if defined(LUA_DEBUG)
LUAI_FUNC Node *LUAH_mainposition (const Table *t, const TValue *key);
LUAI_FUNC int LUAH_isdummy (Node *n);
#endif


#endif
