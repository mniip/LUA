/*
** $Id: Lundump.h,v 1.39 2012/05/08 13:53:33 roberto Exp $
** load precompiled LUA chunks
** See Copyright Notice in LUA.h
*/

#ifndef lundump_h
#define lundump_h

#include "Lobject.h"
#include "Lzio.h"

/* load one chunk; from lundump.c */
LUAI_FUNC Closure* LUAU_undump (LUA_State* L, ZIO* Z, Mbuffer* buff, const char* name);

/* make header; from lundump.c */
LUAI_FUNC void LUAU_header (lu_byte* h);

/* dump one chunk; from ldump.c */
LUAI_FUNC int LUAU_dump (LUA_State* L, const Proto* f, LUA_Writer w, void* data, int strip);

/* data to catch conversion errors */
#define LUAC_TAIL		"\x19\x93\r\n\x1a\n"

/* size in bytes of header of binary files */
#define LUAC_HEADERSIZE		(sizeof(LUA_SIGNATURE)-sizeof(char)+2+6+sizeof(LUAC_TAIL)-sizeof(char))

#endif
