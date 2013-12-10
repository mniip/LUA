/*
** $Id: Lauxlib.h,v 1.120 2011/11/29 15:55:08 roberto Exp $
** Auxiliary functions for building LUA libraries
** See Copyright Notice in LUA.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "LUA.h"



/* extra error code for `LUAL_load' */
#define LUA_ERRFILE     (LUA_ERRERR+1)


typedef struct LUAL_Reg {
  const char *name;
  LUA_CFunction func;
} LUAL_Reg;


LUALIB_API void (LUAL_checkversion_) (LUA_State *L, LUA_Number ver);
#define LUAL_checkversion(L)	LUAL_checkversion_(L, LUA_VERSION_NUM)

LUALIB_API int (LUAL_getmetafield) (LUA_State *L, int obj, const char *e);
LUALIB_API int (LUAL_callmeta) (LUA_State *L, int obj, const char *e);
LUALIB_API const char *(LUAL_tolstring) (LUA_State *L, int idx, size_t *len);
LUALIB_API int (LUAL_argerror) (LUA_State *L, int numarg, const char *extramsg);
LUALIB_API const char *(LUAL_checklstring) (LUA_State *L, int numArg,
                                                          size_t *l);
LUALIB_API const char *(LUAL_optlstring) (LUA_State *L, int numArg,
                                          const char *def, size_t *l);
LUALIB_API LUA_Number (LUAL_checknumber) (LUA_State *L, int numArg);
LUALIB_API LUA_Number (LUAL_optnumber) (LUA_State *L, int nArg, LUA_Number def);

LUALIB_API LUA_Integer (LUAL_checkinteger) (LUA_State *L, int numArg);
LUALIB_API LUA_Integer (LUAL_optinteger) (LUA_State *L, int nArg,
                                          LUA_Integer def);
LUALIB_API LUA_Unsigned (LUAL_checkunsigned) (LUA_State *L, int numArg);
LUALIB_API LUA_Unsigned (LUAL_optunsigned) (LUA_State *L, int numArg,
                                            LUA_Unsigned def);

LUALIB_API void (LUAL_checkstack) (LUA_State *L, int sz, const char *msg);
LUALIB_API void (LUAL_checktype) (LUA_State *L, int narg, int t);
LUALIB_API void (LUAL_checkany) (LUA_State *L, int narg);

LUALIB_API int   (LUAL_newmetatable) (LUA_State *L, const char *tname);
LUALIB_API void  (LUAL_setmetatable) (LUA_State *L, const char *tname);
LUALIB_API void *(LUAL_testudata) (LUA_State *L, int ud, const char *tname);
LUALIB_API void *(LUAL_checkudata) (LUA_State *L, int ud, const char *tname);

LUALIB_API void (LUAL_where) (LUA_State *L, int lvl);
LUALIB_API int (LUAL_error) (LUA_State *L, const char *fmt, ...);

LUALIB_API int (LUAL_checkoption) (LUA_State *L, int narg, const char *def,
                                   const char *const lst[]);

LUALIB_API int (LUAL_fileresult) (LUA_State *L, int stat, const char *fname);
LUALIB_API int (LUAL_execresult) (LUA_State *L, int stat);

/* pre-defined references */
#define LUA_NOREF       (-2)
#define LUA_REFNIL      (-1)

LUALIB_API int (LUAL_ref) (LUA_State *L, int t);
LUALIB_API void (LUAL_unref) (LUA_State *L, int t, int ref);

LUALIB_API int (LUAL_loadfilex) (LUA_State *L, const char *filename,
                                               const char *mode);

#define LUAL_loadfile(L,f)	LUAL_loadfilex(L,f,NULL)

LUALIB_API int (LUAL_loadbufferx) (LUA_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
LUALIB_API int (LUAL_loadstring) (LUA_State *L, const char *s);

LUALIB_API LUA_State *(LUAL_newstate) (void);

LUALIB_API int (LUAL_len) (LUA_State *L, int idx);

LUALIB_API const char *(LUAL_gsub) (LUA_State *L, const char *s, const char *p,
                                                  const char *r);

LUALIB_API void (LUAL_setfuncs) (LUA_State *L, const LUAL_Reg *l, int nup);

LUALIB_API int (LUAL_getsubtable) (LUA_State *L, int idx, const char *fname);

LUALIB_API void (LUAL_traceback) (LUA_State *L, LUA_State *L1,
                                  const char *msg, int level);

LUALIB_API void (LUAL_requiref) (LUA_State *L, const char *modname,
                                 LUA_CFunction openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


#define LUAL_newlibtable(L,l)	\
  LUA_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define LUAL_newlib(L,l)	(LUAL_newlibtable(L,l), LUAL_setfuncs(L,l,0))

#define LUAL_argcheck(L, cond,numarg,extramsg)	\
		((void)((cond) || LUAL_argerror(L, (numarg), (extramsg))))
#define LUAL_checkstring(L,n)	(LUAL_checklstring(L, (n), NULL))
#define LUAL_optstring(L,n,d)	(LUAL_optlstring(L, (n), (d), NULL))
#define LUAL_checkint(L,n)	((int)LUAL_checkinteger(L, (n)))
#define LUAL_optint(L,n,d)	((int)LUAL_optinteger(L, (n), (d)))
#define LUAL_checklong(L,n)	((long)LUAL_checkinteger(L, (n)))
#define LUAL_optlong(L,n,d)	((long)LUAL_optinteger(L, (n), (d)))

#define LUAL_typename(L,i)	LUA_typename(L, LUA_type(L,(i)))

#define LUAL_dofile(L, fn) \
	(LUAL_loadfile(L, fn) || LUA_pcall(L, 0, LUA_MULTRET, 0))

#define LUAL_dostring(L, s) \
	(LUAL_loadstring(L, s) || LUA_pcall(L, 0, LUA_MULTRET, 0))

#define LUAL_getmetatable(L,n)	(LUA_getfield(L, LUA_REGISTRYINDEX, (n)))

#define LUAL_opt(L,f,n,d)	(LUA_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define LUAL_loadbuffer(L,s,sz,n)	LUAL_loadbufferx(L,s,sz,n,NULL)


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

typedef struct LUAL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  LUA_State *L;
  char initb[LUAL_BUFFERSIZE];  /* initial buffer */
} LUAL_Buffer;


#define LUAL_addchar(B,c) \
  ((void)((B)->n < (B)->size || LUAL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define LUAL_addsize(B,s)	((B)->n += (s))

LUALIB_API void (LUAL_buffinit) (LUA_State *L, LUAL_Buffer *B);
LUALIB_API char *(LUAL_prepbuffsize) (LUAL_Buffer *B, size_t sz);
LUALIB_API void (LUAL_addlstring) (LUAL_Buffer *B, const char *s, size_t l);
LUALIB_API void (LUAL_addstring) (LUAL_Buffer *B, const char *s);
LUALIB_API void (LUAL_addvalue) (LUAL_Buffer *B);
LUALIB_API void (LUAL_pushresult) (LUAL_Buffer *B);
LUALIB_API void (LUAL_pushresultsize) (LUAL_Buffer *B, size_t sz);
LUALIB_API char *(LUAL_buffinitsize) (LUA_State *L, LUAL_Buffer *B, size_t sz);

#define LUAL_prepbuffer(B)	LUAL_prepbuffsize(B, LUAL_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'LUA_FILEHANDLE' and
** initial structure 'LUAL_Stream' (it may contain other fields
** after that initial structure).
*/

#define LUA_FILEHANDLE          "FILE*"


typedef struct LUAL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  LUA_CFunction closef;  /* to close stream (NULL for closed streams) */
} LUAL_Stream;

/* }====================================================== */



/* compatibility with old module system */
#if defined(LUA_COMPAT_MODULE)

LUALIB_API void (LUAL_pushmodule) (LUA_State *L, const char *modname,
                                   int sizehint);
LUALIB_API void (LUAL_openlib) (LUA_State *L, const char *libname,
                                const LUAL_Reg *l, int nup);

#define LUAL_register(L,n,l)	(LUAL_openlib(L,(n),(l),0))

#endif


#endif


