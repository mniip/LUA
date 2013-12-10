/*
** $Id: ldblib.c,v 1.132 2012/01/19 20:14:44 roberto Exp $
** Interface from LUA to its debug API
** See Copyright Notice in LUA.h
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ldblib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


#define HOOKKEY		"_HKEY"



static int db_getregistry (LUA_State *L) {
  LUA_pushvalue(L, LUA_REGISTRYINDEX);
  return 1;
}


static int db_getmetatable (LUA_State *L) {
  LUAL_checkany(L, 1);
  if (!LUA_getmetatable(L, 1)) {
    LUA_pushnil(L);  /* no metatable */
  }
  return 1;
}


static int db_setmetatable (LUA_State *L) {
  int t = LUA_type(L, 2);
  LUAL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                    "nil or table expected");
  LUA_settop(L, 2);
  LUA_setmetatable(L, 1);
  return 1;  /* return 1st argument */
}


static int db_getuservalue (LUA_State *L) {
  if (LUA_type(L, 1) != LUA_TUSERDATA)
    LUA_pushnil(L);
  else
    LUA_getuservalue(L, 1);
  return 1;
}


static int db_setuservalue (LUA_State *L) {
  if (LUA_type(L, 1) == LUA_TLIGHTUSERDATA)
    LUAL_argerror(L, 1, "full userdata expected, got light userdata");
  LUAL_checktype(L, 1, LUA_TUSERDATA);
  if (!LUA_isnoneornil(L, 2))
    LUAL_checktype(L, 2, LUA_TTABLE);
  LUA_settop(L, 2);
  LUA_setuservalue(L, 1);
  return 1;
}


static void settabss (LUA_State *L, const char *i, const char *v) {
  LUA_pushstring(L, v);
  LUA_setfield(L, -2, i);
}


static void settabsi (LUA_State *L, const char *i, int v) {
  LUA_pushinteger(L, v);
  LUA_setfield(L, -2, i);
}


static void settabsb (LUA_State *L, const char *i, int v) {
  LUA_pushboolean(L, v);
  LUA_setfield(L, -2, i);
}


static LUA_State *getthread (LUA_State *L, int *arg) {
  if (LUA_isthread(L, 1)) {
    *arg = 1;
    return LUA_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;
  }
}


static void treatstackoption (LUA_State *L, LUA_State *L1, const char *fname) {
  if (L == L1) {
    LUA_pushvalue(L, -2);
    LUA_remove(L, -3);
  }
  else
    LUA_xmove(L1, L, 1);
  LUA_setfield(L, -2, fname);
}


static int db_getinfo (LUA_State *L) {
  LUA_Debug ar;
  int arg;
  LUA_State *L1 = getthread(L, &arg);
  const char *options = LUAL_optstring(L, arg+2, "flnStu");
  if (LUA_isnumber(L, arg+1)) {
    if (!LUA_getstack(L1, (int)LUA_tointeger(L, arg+1), &ar)) {
      LUA_pushnil(L);  /* level out of range */
      return 1;
    }
  }
  else if (LUA_isfunction(L, arg+1)) {
    LUA_pushfstring(L, ">%s", options);
    options = LUA_tostring(L, -1);
    LUA_pushvalue(L, arg+1);
    LUA_xmove(L, L1, 1);
  }
  else
    return LUAL_argerror(L, arg+1, "function or level expected");
  if (!LUA_getinfo(L1, options, &ar))
    return LUAL_argerror(L, arg+2, "invalid option");
  LUA_createtable(L, 0, 2);
  if (strchr(options, 'S')) {
    settabss(L, "SOURCE", ar.source);
    settabss(L, "SHORT_SRC", ar.short_src);
    settabsi(L, "LINEDEFIEND", ar.linedefined);
    settabsi(L, "LASTLINEDEFINED", ar.lastlinedefined);
    settabss(L, "WHAT", ar.what);
  }
  if (strchr(options, 'l'))
    settabsi(L, "CURRENTLINE", ar.currentline);
  if (strchr(options, 'u')) {
    settabsi(L, "NUPS", ar.nups);
    settabsi(L, "NPARAMS", ar.nparams);
    settabsb(L, "ISVARARG", ar.isvararg);
  }
  if (strchr(options, 'n')) {
    settabss(L, "NAME", ar.name);
    settabss(L, "NAMEWHAT", ar.namewhat);
  }
  if (strchr(options, 't'))
    settabsb(L, "ISTAILCALL", ar.istailcall);
  if (strchr(options, 'L'))
    treatstackoption(L, L1, "ACTIVELINES");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "FUNC");
  return 1;  /* return table */
}


static int db_getlocal (LUA_State *L) {
  int arg;
  LUA_State *L1 = getthread(L, &arg);
  LUA_Debug ar;
  const char *name;
  int nvar = LUAL_checkint(L, arg+2);  /* local-variable index */
  if (LUA_isfunction(L, arg + 1)) {  /* function argument? */
    LUA_pushvalue(L, arg + 1);  /* push function */
    LUA_pushstring(L, LUA_getlocal(L, NULL, nvar));  /* push local name */
    return 1;
  }
  else {  /* stack-level argument */
    if (!LUA_getstack(L1, LUAL_checkint(L, arg+1), &ar))  /* out of range? */
      return LUAL_argerror(L, arg+1, "level out of range");
    name = LUA_getlocal(L1, &ar, nvar);
    if (name) {
      LUA_xmove(L1, L, 1);  /* push local value */
      LUA_pushstring(L, name);  /* push name */
      LUA_pushvalue(L, -2);  /* re-order */
      return 2;
    }
    else {
      LUA_pushnil(L);  /* no name (nor value) */
      return 1;
    }
  }
}


static int db_setlocal (LUA_State *L) {
  int arg;
  LUA_State *L1 = getthread(L, &arg);
  LUA_Debug ar;
  if (!LUA_getstack(L1, LUAL_checkint(L, arg+1), &ar))  /* out of range? */
    return LUAL_argerror(L, arg+1, "level out of range");
  LUAL_checkany(L, arg+3);
  LUA_settop(L, arg+3);
  LUA_xmove(L, L1, 1);
  LUA_pushstring(L, LUA_setlocal(L1, &ar, LUAL_checkint(L, arg+2)));
  return 1;
}


static int auxupvalue (LUA_State *L, int get) {
  const char *name;
  int n = LUAL_checkint(L, 2);
  LUAL_checktype(L, 1, LUA_TFUNCTION);
  name = get ? LUA_getupvalue(L, 1, n) : LUA_setupvalue(L, 1, n);
  if (name == NULL) return 0;
  LUA_pushstring(L, name);
  LUA_insert(L, -(get+1));
  return get + 1;
}


static int db_getupvalue (LUA_State *L) {
  return auxupvalue(L, 1);
}


static int db_setupvalue (LUA_State *L) {
  LUAL_checkany(L, 3);
  return auxupvalue(L, 0);
}


static int checkupval (LUA_State *L, int argf, int argnup) {
  LUA_Debug ar;
  int nup = LUAL_checkint(L, argnup);
  LUAL_checktype(L, argf, LUA_TFUNCTION);
  LUA_pushvalue(L, argf);
  LUA_getinfo(L, ">u", &ar);
  LUAL_argcheck(L, 1 <= nup && nup <= ar.nups, argnup, "invalid upvalue index");
  return nup;
}


static int db_upvalueid (LUA_State *L) {
  int n = checkupval(L, 1, 2);
  LUA_pushlightuserdata(L, LUA_upvalueid(L, 1, n));
  return 1;
}


static int db_upvaluejoin (LUA_State *L) {
  int n1 = checkupval(L, 1, 2);
  int n2 = checkupval(L, 3, 4);
  LUAL_argcheck(L, !LUA_iscfunction(L, 1), 1, "LUA function expected");
  LUAL_argcheck(L, !LUA_iscfunction(L, 3), 3, "LUA function expected");
  LUA_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}


#define gethooktable(L)	LUAL_getsubtable(L, LUA_REGISTRYINDEX, HOOKKEY)


static void hookf (LUA_State *L, LUA_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  gethooktable(L);
  LUA_pushthread(L);
  LUA_rawget(L, -2);
  if (LUA_isfunction(L, -1)) {
    LUA_pushstring(L, hooknames[(int)ar->event]);
    if (ar->currentline >= 0)
      LUA_pushinteger(L, ar->currentline);
    else LUA_pushnil(L);
    LUA_assert(LUA_getinfo(L, "lS", ar));
    LUA_call(L, 2, 0);
  }
}


static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
  if (strchr(smask, 'r')) mask |= LUA_MASKRET;
  if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
  if (count > 0) mask |= LUA_MASKCOUNT;
  return mask;
}


static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & LUA_MASKCALL) smask[i++] = 'c';
  if (mask & LUA_MASKRET) smask[i++] = 'r';
  if (mask & LUA_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}


static int db_sethook (LUA_State *L) {
  int arg, mask, count;
  LUA_Hook func;
  LUA_State *L1 = getthread(L, &arg);
  if (LUA_isnoneornil(L, arg+1)) {
    LUA_settop(L, arg+1);
    func = NULL; mask = 0; count = 0;  /* turn off hooks */
  }
  else {
    const char *smask = LUAL_checkstring(L, arg+2);
    LUAL_checktype(L, arg+1, LUA_TFUNCTION);
    count = LUAL_optint(L, arg+3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (gethooktable(L) == 0) {  /* creating hook table? */
    LUA_pushstring(L, "k");
    LUA_setfield(L, -2, "__mode");  /** hooktable.__mode = "k" */
    LUA_pushvalue(L, -1);
    LUA_setmetatable(L, -2);  /* setmetatable(hooktable) = hooktable */
  }
  LUA_pushthread(L1); LUA_xmove(L1, L, 1);
  LUA_pushvalue(L, arg+1);
  LUA_rawset(L, -3);  /* set new hook */
  LUA_sethook(L1, func, mask, count);  /* set hooks */
  return 0;
}


static int db_gethook (LUA_State *L) {
  int arg;
  LUA_State *L1 = getthread(L, &arg);
  char buff[5];
  int mask = LUA_gethookmask(L1);
  LUA_Hook hook = LUA_gethook(L1);
  if (hook != NULL && hook != hookf)  /* external hook? */
    LUA_pushliteral(L, "external hook");
  else {
    gethooktable(L);
    LUA_pushthread(L1); LUA_xmove(L1, L, 1);
    LUA_rawget(L, -2);   /* get hook */
    LUA_remove(L, -2);  /* remove hook table */
  }
  LUA_pushstring(L, unmakemask(mask, buff));
  LUA_pushinteger(L, LUA_gethookcount(L1));
  return 3;
}


static int db_debug (LUA_State *L) {
  for (;;) {
    char buffer[250];
    LUAi_writestringerror("%s", "LUA_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == 0 ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (LUAL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        LUA_pcall(L, 0, 0, 0))
      LUAi_writestringerror("%s\n", LUA_tostring(L, -1));
    LUA_settop(L, 0);  /* remove eventual returns */
  }
}


static int db_traceback (LUA_State *L) {
  int arg;
  LUA_State *L1 = getthread(L, &arg);
  const char *msg = LUA_tostring(L, arg + 1);
  if (msg == NULL && !LUA_isnoneornil(L, arg + 1))  /* non-string 'msg'? */
    LUA_pushvalue(L, arg + 1);  /* return it untouched */
  else {
    int level = LUAL_optint(L, arg + 2, (L == L1) ? 1 : 0);
    LUAL_traceback(L, L1, msg, level);
  }
  return 1;
}


static const LUAL_Reg dblib[] = {
  {"DEBUG", db_debug},
  {"GETUSERVALUE", db_getuservalue},
  {"GETHOOK", db_gethook},
  {"GETINFO", db_getinfo},
  {"GETLOCAL", db_getlocal},
  {"GETREGISTRY", db_getregistry},
  {"GETMETATABLE", db_getmetatable},
  {"GETUPVALUE", db_getupvalue},
  {"UPVALUEJOIN", db_upvaluejoin},
  {"UPVALUEID", db_upvalueid},
  {"SETUSERVALUE", db_setuservalue},
  {"SETHOOK", db_sethook},
  {"SETLOCAL", db_setlocal},
  {"SETMETATABLE", db_setmetatable},
  {"SETUPVALUE", db_setupvalue},
  {"TRACEBACK", db_traceback},
  {NULL, NULL}
};


LUAMOD_API int LUAopen_debug (LUA_State *L) {
  LUAL_newlib(L, dblib);
  return 1;
}

