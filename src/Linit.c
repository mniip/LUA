/*
** $Id: linit.c,v 1.32 2011/04/08 19:17:36 roberto Exp $
** Initialization of libraries for LUA.c and other clients
** See Copyright Notice in LUA.h
*/


/*
** If you embed LUA in your program and need to open the standard
** libraries, call LUAL_openlibs in your program. If you need a
** different set of libraries, copy this file to your project and edit
** it to suit your needs.
*/


#define linit_c
#define LUA_LIB

#include "LUA.h"

#include "LUAlib.h"
#include "Lauxlib.h"


/*
** these libs are loaded by LUA.c and are readily available to any LUA
** program
*/
static const LUAL_Reg loadedlibs[] = {
  {"_G", LUAopen_base},
  {LUA_LOADLIBNAME, LUAopen_package},
  {LUA_COLIBNAME, LUAopen_coroutine},
  {LUA_TABLIBNAME, LUAopen_table},
  {LUA_IOLIBNAME, LUAopen_io},
  {LUA_OSLIBNAME, LUAopen_os},
  {LUA_STRLIBNAME, LUAopen_string},
  {LUA_BITLIBNAME, LUAopen_bit32},
  {LUA_MATHLIBNAME, LUAopen_math},
  {LUA_DBLIBNAME, LUAopen_debug},
  {NULL, NULL}
};


/*
** these libs are preloaded and must be required before used
*/
static const LUAL_Reg preloadedlibs[] = {
  {NULL, NULL}
};


LUALIB_API void LUAL_openlibs (LUA_State *L) {
  const LUAL_Reg *lib;
  /* call open functions from 'loadedlibs' and set results to global table */
  for (lib = loadedlibs; lib->func; lib++) {
    LUAL_requiref(L, lib->name, lib->func, 1);
    LUA_pop(L, 1);  /* remove lib */
  }
  /* add open functions from 'preloadedlibs' into 'package.preload' table */
  LUAL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");
  for (lib = preloadedlibs; lib->func; lib++) {
    LUA_pushcfunction(L, lib->func);
    LUA_setfield(L, -2, lib->name);
  }
  LUA_pop(L, 1);  /* remove _PRELOAD table */
}

