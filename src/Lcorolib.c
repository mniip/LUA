/*
** $Id: lcorolib.c,v 1.5 2013/02/21 13:44:53 roberto Exp $
** Coroutine Library
** See Copyright Notice in LUA.h
*/


#include <stdlib.h>


#define lcorolib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


static int auxresume (LUA_State *L, LUA_State *co, int narg) {
  int status;
  if (!LUA_checkstack(co, narg)) {
    LUA_pushliteral(L, "too many arguments to resume");
    return -1;  /* error flag */
  }
  if (LUA_status(co) == LUA_OK && LUA_gettop(co) == 0) {
    LUA_pushliteral(L, "cannot resume dead coroutine");
    return -1;  /* error flag */
  }
  LUA_xmove(L, co, narg);
  status = LUA_resume(co, L, narg);
  if (status == LUA_OK || status == LUA_YIELD) {
    int nres = LUA_gettop(co);
    if (!LUA_checkstack(L, nres + 1)) {
      LUA_pop(co, nres);  /* remove results anyway */
      LUA_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    LUA_xmove(co, L, nres);  /* move yielded values */
    return nres;
  }
  else {
    LUA_xmove(co, L, 1);  /* move error message */
    return -1;  /* error flag */
  }
}


static int LUAB_coresume (LUA_State *L) {
  LUA_State *co = LUA_tothread(L, 1);
  int r;
  LUAL_argcheck(L, co, 1, "coroutine expected");
  r = auxresume(L, co, LUA_gettop(L) - 1);
  if (r < 0) {
    LUA_pushboolean(L, 0);
    LUA_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    LUA_pushboolean(L, 1);
    LUA_insert(L, -(r + 1));
    return r + 1;  /* return true + `resume' returns */
  }
}


static int LUAB_auxwrap (LUA_State *L) {
  LUA_State *co = LUA_tothread(L, LUA_upvalueindex(1));
  int r = auxresume(L, co, LUA_gettop(L));
  if (r < 0) {
    if (LUA_isstring(L, -1)) {  /* error object is a string? */
      LUAL_where(L, 1);  /* add extra info */
      LUA_insert(L, -2);
      LUA_concat(L, 2);
    }
    return LUA_error(L);  /* propagate error */
  }
  return r;
}


static int LUAB_cocreate (LUA_State *L) {
  LUA_State *NL;
  LUAL_checktype(L, 1, LUA_TFUNCTION);
  NL = LUA_newthread(L);
  LUA_pushvalue(L, 1);  /* move function to top */
  LUA_xmove(L, NL, 1);  /* move function from L to NL */
  return 1;
}


static int LUAB_cowrap (LUA_State *L) {
  LUAB_cocreate(L);
  LUA_pushcclosure(L, LUAB_auxwrap, 1);
  return 1;
}


static int LUAB_yield (LUA_State *L) {
  return LUA_yield(L, LUA_gettop(L));
}


static int LUAB_costatus (LUA_State *L) {
  LUA_State *co = LUA_tothread(L, 1);
  LUAL_argcheck(L, co, 1, "coroutine expected");
  if (L == co) LUA_pushliteral(L, "running");
  else {
    switch (LUA_status(co)) {
      case LUA_YIELD:
        LUA_pushliteral(L, "suspended");
        break;
      case LUA_OK: {
        LUA_Debug ar;
        if (LUA_getstack(co, 0, &ar) > 0)  /* does it have frames? */
          LUA_pushliteral(L, "normal");  /* it is running */
        else if (LUA_gettop(co) == 0)
            LUA_pushliteral(L, "dead");
        else
          LUA_pushliteral(L, "suspended");  /* initial state */
        break;
      }
      default:  /* some error occurred */
        LUA_pushliteral(L, "dead");
        break;
    }
  }
  return 1;
}


static int LUAB_corunning (LUA_State *L) {
  int ismain = LUA_pushthread(L);
  LUA_pushboolean(L, ismain);
  return 2;
}


static const LUAL_Reg co_funcs[] = {
  {"CREATE", LUAB_cocreate},
  {"RESUME", LUAB_coresume},
  {"RUNNING", LUAB_corunning},
  {"STATUS", LUAB_costatus},
  {"WRAP", LUAB_cowrap},
  {"YIELD", LUAB_yield},
  {NULL, NULL}
};



LUAMOD_API int LUAopen_coroutine (LUA_State *L) {
  LUAL_newlib(L, co_funcs);
  return 1;
}

