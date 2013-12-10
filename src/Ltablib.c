/*
** $Id: ltablib.c,v 1.65 2013/03/07 18:17:24 roberto Exp $
** Library for Table Manipulation
** See Copyright Notice in LUA.h
*/


#include <stddef.h>

#define ltablib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


#define aux_getn(L,n)	(LUAL_checktype(L, n, LUA_TTABLE), LUAL_len(L, n))



#if defined(LUA_COMPAT_MAXN)
static int maxn (LUA_State *L) {
  LUA_Number max = 0;
  LUAL_checktype(L, 1, LUA_TTABLE);
  LUA_pushnil(L);  /* first key */
  while (LUA_next(L, 1)) {
    LUA_pop(L, 1);  /* remove value */
    if (LUA_type(L, -1) == LUA_TNUMBER) {
      LUA_Number v = LUA_tonumber(L, -1);
      if (v > max) max = v;
    }
  }
  LUA_pushnumber(L, max);
  return 1;
}
#endif


static int tinsert (LUA_State *L) {
  int e = aux_getn(L, 1) + 1;  /* first empty element */
  int pos;  /* where to insert new element */
  switch (LUA_gettop(L)) {
    case 2: {  /* called with only 2 arguments */
      pos = e;  /* insert new element at the end */
      break;
    }
    case 3: {
      int i;
      pos = LUAL_checkint(L, 2);  /* 2nd argument is the position */
      LUAL_argcheck(L, 1 <= pos && pos <= e, 2, "position out of bounds");
      for (i = e; i > pos; i--) {  /* move up elements */
        LUA_rawgeti(L, 1, i-1);
        LUA_rawseti(L, 1, i);  /* t[i] = t[i-1] */
      }
      break;
    }
    default: {
      return LUAL_error(L, "wrong number of arguments to " LUA_QL("INSERT"));
    }
  }
  LUA_rawseti(L, 1, pos);  /* t[pos] = v */
  return 0;
}


static int tremove (LUA_State *L) {
  int size = aux_getn(L, 1);
  int pos = LUAL_optint(L, 2, size);
  if (pos != size)  /* validate 'pos' if given */
    LUAL_argcheck(L, 1 <= pos && pos <= size + 1, 1, "position out of bounds");
  LUA_rawgeti(L, 1, pos);  /* result = t[pos] */
  for ( ; pos < size; pos++) {
    LUA_rawgeti(L, 1, pos+1);
    LUA_rawseti(L, 1, pos);  /* t[pos] = t[pos+1] */
  }
  LUA_pushnil(L);
  LUA_rawseti(L, 1, pos);  /* t[pos] = nil */
  return 1;
}


static void addfield (LUA_State *L, LUAL_Buffer *b, int i) {
  LUA_rawgeti(L, 1, i);
  if (!LUA_isstring(L, -1))
    LUAL_error(L, "invalid value (%s) at index %d in table for "
                  LUA_QL("CONCAT"), LUAL_typename(L, -1), i);
  LUAL_addvalue(b);
}


static int tconcat (LUA_State *L) {
  LUAL_Buffer b;
  size_t lsep;
  int i, last;
  const char *sep = LUAL_optlstring(L, 2, "", &lsep);
  LUAL_checktype(L, 1, LUA_TTABLE);
  i = LUAL_optint(L, 3, 1);
  last = LUAL_opt(L, LUAL_checkint, 4, LUAL_len(L, 1));
  LUAL_buffinit(L, &b);
  for (; i < last; i++) {
    addfield(L, &b, i);
    LUAL_addlstring(&b, sep, lsep);
  }
  if (i == last)  /* add last value (if interval was not empty) */
    addfield(L, &b, i);
  LUAL_pushresult(&b);
  return 1;
}


/*
** {======================================================
** Pack/unpack
** =======================================================
*/

static int pack (LUA_State *L) {
  int n = LUA_gettop(L);  /* number of elements to pack */
  LUA_createtable(L, n, 1);  /* create result table */
  LUA_pushinteger(L, n);
  LUA_setfield(L, -2, "n");  /* t.n = number of elements */
  if (n > 0) {  /* at least one element? */
    int i;
    LUA_pushvalue(L, 1);
    LUA_rawseti(L, -2, 1);  /* insert first element */
    LUA_replace(L, 1);  /* move table into index 1 */
    for (i = n; i >= 2; i--)  /* assign other elements */
      LUA_rawseti(L, 1, i);
  }
  return 1;  /* return table */
}


static int unpack (LUA_State *L) {
  int i, e, n;
  LUAL_checktype(L, 1, LUA_TTABLE);
  i = LUAL_optint(L, 2, 1);
  e = LUAL_opt(L, LUAL_checkint, 3, LUAL_len(L, 1));
  if (i > e) return 0;  /* empty range */
  n = e - i + 1;  /* number of elements */
  if (n <= 0 || !LUA_checkstack(L, n))  /* n <= 0 means arith. overflow */
    return LUAL_error(L, "too many results to unpack");
  LUA_rawgeti(L, 1, i);  /* push arg[i] (avoiding overflow problems) */
  while (i++ < e)  /* push arg[i + 1...e] */
    LUA_rawgeti(L, 1, i);
  return n;
}

/* }====================================================== */



/*
** {======================================================
** Quicksort
** (based on `Algorithms in MODULA-3', Robert Sedgewick;
**  Addison-Wesley, 1993.)
** =======================================================
*/


static void set2 (LUA_State *L, int i, int j) {
  LUA_rawseti(L, 1, i);
  LUA_rawseti(L, 1, j);
}

static int sort_comp (LUA_State *L, int a, int b) {
  if (!LUA_isnil(L, 2)) {  /* function? */
    int res;
    LUA_pushvalue(L, 2);
    LUA_pushvalue(L, a-1);  /* -1 to compensate function */
    LUA_pushvalue(L, b-2);  /* -2 to compensate function and `a' */
    LUA_call(L, 2, 1);
    res = LUA_toboolean(L, -1);
    LUA_pop(L, 1);
    return res;
  }
  else  /* a < b? */
    return LUA_compare(L, a, b, LUA_OPLT);
}

static void auxsort (LUA_State *L, int l, int u) {
  while (l < u) {  /* for tail recursion */
    int i, j;
    /* sort elements a[l], a[(l+u)/2] and a[u] */
    LUA_rawgeti(L, 1, l);
    LUA_rawgeti(L, 1, u);
    if (sort_comp(L, -1, -2))  /* a[u] < a[l]? */
      set2(L, l, u);  /* swap a[l] - a[u] */
    else
      LUA_pop(L, 2);
    if (u-l == 1) break;  /* only 2 elements */
    i = (l+u)/2;
    LUA_rawgeti(L, 1, i);
    LUA_rawgeti(L, 1, l);
    if (sort_comp(L, -2, -1))  /* a[i]<a[l]? */
      set2(L, i, l);
    else {
      LUA_pop(L, 1);  /* remove a[l] */
      LUA_rawgeti(L, 1, u);
      if (sort_comp(L, -1, -2))  /* a[u]<a[i]? */
        set2(L, i, u);
      else
        LUA_pop(L, 2);
    }
    if (u-l == 2) break;  /* only 3 elements */
    LUA_rawgeti(L, 1, i);  /* Pivot */
    LUA_pushvalue(L, -1);
    LUA_rawgeti(L, 1, u-1);
    set2(L, i, u-1);
    /* a[l] <= P == a[u-1] <= a[u], only need to sort from l+1 to u-2 */
    i = l; j = u-1;
    for (;;) {  /* invariant: a[l..i] <= P <= a[j..u] */
      /* repeat ++i until a[i] >= P */
      while (LUA_rawgeti(L, 1, ++i), sort_comp(L, -1, -2)) {
        if (i>=u) LUAL_error(L, "invalid order function for sorting");
        LUA_pop(L, 1);  /* remove a[i] */
      }
      /* repeat --j until a[j] <= P */
      while (LUA_rawgeti(L, 1, --j), sort_comp(L, -3, -1)) {
        if (j<=l) LUAL_error(L, "invalid order function for sorting");
        LUA_pop(L, 1);  /* remove a[j] */
      }
      if (j<i) {
        LUA_pop(L, 3);  /* pop pivot, a[i], a[j] */
        break;
      }
      set2(L, i, j);
    }
    LUA_rawgeti(L, 1, u-1);
    LUA_rawgeti(L, 1, i);
    set2(L, u-1, i);  /* swap pivot (a[u-1]) with a[i] */
    /* a[l..i-1] <= a[i] == P <= a[i+1..u] */
    /* adjust so that smaller half is in [j..i] and larger one in [l..u] */
    if (i-l < u-i) {
      j=l; i=i-1; l=i+2;
    }
    else {
      j=i+1; i=u; u=j-2;
    }
    auxsort(L, j, i);  /* call recursively the smaller one */
  }  /* repeat the routine for the larger one */
}

static int sort (LUA_State *L) {
  int n = aux_getn(L, 1);
  LUAL_checkstack(L, 40, "");  /* assume array is smaller than 2^40 */
  if (!LUA_isnoneornil(L, 2))  /* is there a 2nd argument? */
    LUAL_checktype(L, 2, LUA_TFUNCTION);
  LUA_settop(L, 2);  /* make sure there is two arguments */
  auxsort(L, 1, n);
  return 0;
}

/* }====================================================== */


static const LUAL_Reg tab_funcs[] = {
  {"CONCAT", tconcat},
#if defined(LUA_COMPAT_MAXN)
  {"MAXN", maxn},
#endif
  {"INSERT", tinsert},
  {"PACK", pack},
  {"UNPACK", unpack},
  {"REMOVE", tremove},
  {"SORT", sort},
  {NULL, NULL}
};


LUAMOD_API int LUAopen_table (LUA_State *L) {
  LUAL_newlib(L, tab_funcs);
#if defined(LUA_COMPAT_UNPACK)
  /* _G.unpack = table.unpack */
  LUA_getfield(L, -1, "UNPACK");
  LUA_setglobal(L, "UNPACK");
#endif
  return 1;
}

