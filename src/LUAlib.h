/*
** $Id: LUAlib.h,v 1.43 2011/12/08 12:11:37 roberto Exp $
** LUA standard libraries
** See Copyright Notice in LUA.h
*/


#ifndef LUAlib_h
#define LUAlib_h

#include "LUA.h"



LUAMOD_API int (LUAopen_base) (LUA_State *L);

#define LUA_COLIBNAME	"COROUTINE"
LUAMOD_API int (LUAopen_coroutine) (LUA_State *L);

#define LUA_TABLIBNAME	"TABLE"
LUAMOD_API int (LUAopen_table) (LUA_State *L);

#define LUA_IOLIBNAME	"IO"
LUAMOD_API int (LUAopen_io) (LUA_State *L);

#define LUA_OSLIBNAME	"OS"
LUAMOD_API int (LUAopen_os) (LUA_State *L);

#define LUA_STRLIBNAME	"STRING"
LUAMOD_API int (LUAopen_string) (LUA_State *L);

#define LUA_BITLIBNAME	"BIT32"
LUAMOD_API int (LUAopen_bit32) (LUA_State *L);

#define LUA_MATHLIBNAME	"MATH"
LUAMOD_API int (LUAopen_math) (LUA_State *L);

#define LUA_DBLIBNAME	"DEBUG"
LUAMOD_API int (LUAopen_debug) (LUA_State *L);

#define LUA_LOADLIBNAME	"PACKAGE"
LUAMOD_API int (LUAopen_package) (LUA_State *L);


/* open all previous libraries */
LUALIB_API void (LUAL_openlibs) (LUA_State *L);



#if !defined(LUA_assert)
#define LUA_assert(x)	((void)0)
#endif


#endif
