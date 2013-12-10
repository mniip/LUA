/*
** $Id: LUA.h,v 1.285 2013/03/15 13:04:22 roberto Exp $
** LUA - A Scripting Language
** LUA.org, PUC-Rio, Brazil (http://www.LUA.org)
** See Copyright Notice at the end of this file
*/


#ifndef LUA_h
#define LUA_h

#include <stdarg.h>
#include <stddef.h>


#include "LUAconf.h"


#define LUA_VERSION_MAJOR	"5"
#define LUA_VERSION_MINOR	"2"
#define LUA_VERSION_NUM		502
#define LUA_VERSION_RELEASE	"2"

#define LUA_VERSION	"LUA " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT	LUA_RELEASE " Copyright (C) 1994-2013 lua.org, PUC-Rio"
#define LUA_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


/* mark for precompiled code ('<esc>LUA') */
#define LUA_SIGNATURE	"\033LUA"

/* option for multiple returns in 'LUA_pcall' and 'LUA_call' */
#define LUA_MULTRET	(-1)


/*
** pseudo-indices
*/
#define LUA_REGISTRYINDEX	LUAI_FIRSTPSEUDOIDX
#define LUA_upvalueindex(i)	(LUA_REGISTRYINDEX - (i))


/* thread status */
#define LUA_OK		0
#define LUA_YIELD	1
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3
#define LUA_ERRMEM	4
#define LUA_ERRGCMM	5
#define LUA_ERRERR	6


typedef struct LUA_State LUA_State;

typedef int (*LUA_CFunction) (LUA_State *L);


/*
** functions that read/write blocks when loading/dumping LUA chunks
*/
typedef const char * (*LUA_Reader) (LUA_State *L, void *ud, size_t *sz);

typedef int (*LUA_Writer) (LUA_State *L, const void* p, size_t sz, void* ud);


/*
** prototype for memory-allocation functions
*/
typedef void * (*LUA_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/*
** basic types
*/
#define LUA_TNONE		(-1)

#define LUA_TNIL		0
#define LUA_TBOOLEAN		1
#define LUA_TLIGHTUSERDATA	2
#define LUA_TNUMBER		3
#define LUA_TSTRING		4
#define LUA_TTABLE		5
#define LUA_TFUNCTION		6
#define LUA_TUSERDATA		7
#define LUA_TTHREAD		8

#define LUA_NUMTAGS		9



/* minimum LUA stack available to a C function */
#define LUA_MINSTACK	20


/* predefined values in the registry */
#define LUA_RIDX_MAINTHREAD	1
#define LUA_RIDX_GLOBALS	2
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS


/* type of numbers in LUA */
typedef LUA_NUMBER LUA_Number;


/* type for integer functions */
typedef LUA_INTEGER LUA_Integer;

/* unsigned integer type */
typedef LUA_UNSIGNED LUA_Unsigned;



/*
** generic extra include file
*/
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/*
** RCS ident string
*/
extern const char LUA_ident[];


/*
** state manipulation
*/
LUA_API LUA_State *(LUA_newstate) (LUA_Alloc f, void *ud);
LUA_API void       (LUA_close) (LUA_State *L);
LUA_API LUA_State *(LUA_newthread) (LUA_State *L);

LUA_API LUA_CFunction (LUA_atpanic) (LUA_State *L, LUA_CFunction panicf);


LUA_API const LUA_Number *(LUA_version) (LUA_State *L);


/*
** basic stack manipulation
*/
LUA_API int   (LUA_absindex) (LUA_State *L, int idx);
LUA_API int   (LUA_gettop) (LUA_State *L);
LUA_API void  (LUA_settop) (LUA_State *L, int idx);
LUA_API void  (LUA_pushvalue) (LUA_State *L, int idx);
LUA_API void  (LUA_remove) (LUA_State *L, int idx);
LUA_API void  (LUA_insert) (LUA_State *L, int idx);
LUA_API void  (LUA_replace) (LUA_State *L, int idx);
LUA_API void  (LUA_copy) (LUA_State *L, int fromidx, int toidx);
LUA_API int   (LUA_checkstack) (LUA_State *L, int sz);

LUA_API void  (LUA_xmove) (LUA_State *from, LUA_State *to, int n);


/*
** access functions (stack -> C)
*/

LUA_API int             (LUA_isnumber) (LUA_State *L, int idx);
LUA_API int             (LUA_isstring) (LUA_State *L, int idx);
LUA_API int             (LUA_iscfunction) (LUA_State *L, int idx);
LUA_API int             (LUA_isuserdata) (LUA_State *L, int idx);
LUA_API int             (LUA_type) (LUA_State *L, int idx);
LUA_API const char     *(LUA_typename) (LUA_State *L, int tp);

LUA_API LUA_Number      (LUA_tonumberx) (LUA_State *L, int idx, int *isnum);
LUA_API LUA_Integer     (LUA_tointegerx) (LUA_State *L, int idx, int *isnum);
LUA_API LUA_Unsigned    (LUA_tounsignedx) (LUA_State *L, int idx, int *isnum);
LUA_API int             (LUA_toboolean) (LUA_State *L, int idx);
LUA_API const char     *(LUA_tolstring) (LUA_State *L, int idx, size_t *len);
LUA_API size_t          (LUA_rawlen) (LUA_State *L, int idx);
LUA_API LUA_CFunction   (LUA_tocfunction) (LUA_State *L, int idx);
LUA_API void	       *(LUA_touserdata) (LUA_State *L, int idx);
LUA_API LUA_State      *(LUA_tothread) (LUA_State *L, int idx);
LUA_API const void     *(LUA_topointer) (LUA_State *L, int idx);


/*
** Comparison and arithmetic functions
*/

#define LUA_OPADD	0	/* ORDER TM */
#define LUA_OPSUB	1
#define LUA_OPMUL	2
#define LUA_OPDIV	3
#define LUA_OPMOD	4
#define LUA_OPPOW	5
#define LUA_OPUNM	6

LUA_API void  (LUA_arith) (LUA_State *L, int op);

#define LUA_OPEQ	0
#define LUA_OPLT	1
#define LUA_OPLE	2

LUA_API int   (LUA_rawequal) (LUA_State *L, int idx1, int idx2);
LUA_API int   (LUA_compare) (LUA_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
LUA_API void        (LUA_pushnil) (LUA_State *L);
LUA_API void        (LUA_pushnumber) (LUA_State *L, LUA_Number n);
LUA_API void        (LUA_pushinteger) (LUA_State *L, LUA_Integer n);
LUA_API void        (LUA_pushunsigned) (LUA_State *L, LUA_Unsigned n);
LUA_API const char *(LUA_pushlstring) (LUA_State *L, const char *s, size_t l);
LUA_API const char *(LUA_pushstring) (LUA_State *L, const char *s);
LUA_API const char *(LUA_pushvfstring) (LUA_State *L, const char *fmt,
                                                      va_list argp);
LUA_API const char *(LUA_pushfstring) (LUA_State *L, const char *fmt, ...);
LUA_API void  (LUA_pushcclosure) (LUA_State *L, LUA_CFunction fn, int n);
LUA_API void  (LUA_pushboolean) (LUA_State *L, int b);
LUA_API void  (LUA_pushlightuserdata) (LUA_State *L, void *p);
LUA_API int   (LUA_pushthread) (LUA_State *L);


/*
** get functions (LUA -> stack)
*/
LUA_API void  (LUA_getglobal) (LUA_State *L, const char *var);
LUA_API void  (LUA_gettable) (LUA_State *L, int idx);
LUA_API void  (LUA_getfield) (LUA_State *L, int idx, const char *k);
LUA_API void  (LUA_rawget) (LUA_State *L, int idx);
LUA_API void  (LUA_rawgeti) (LUA_State *L, int idx, int n);
LUA_API void  (LUA_rawgetp) (LUA_State *L, int idx, const void *p);
LUA_API void  (LUA_createtable) (LUA_State *L, int narr, int nrec);
LUA_API void *(LUA_newuserdata) (LUA_State *L, size_t sz);
LUA_API int   (LUA_getmetatable) (LUA_State *L, int objindex);
LUA_API void  (LUA_getuservalue) (LUA_State *L, int idx);


/*
** set functions (stack -> LUA)
*/
LUA_API void  (LUA_setglobal) (LUA_State *L, const char *var);
LUA_API void  (LUA_settable) (LUA_State *L, int idx);
LUA_API void  (LUA_setfield) (LUA_State *L, int idx, const char *k);
LUA_API void  (LUA_rawset) (LUA_State *L, int idx);
LUA_API void  (LUA_rawseti) (LUA_State *L, int idx, int n);
LUA_API void  (LUA_rawsetp) (LUA_State *L, int idx, const void *p);
LUA_API int   (LUA_setmetatable) (LUA_State *L, int objindex);
LUA_API void  (LUA_setuservalue) (LUA_State *L, int idx);


/*
** 'load' and 'call' functions (load and run LUA code)
*/
LUA_API void  (LUA_callk) (LUA_State *L, int nargs, int nresults, int ctx,
                           LUA_CFunction k);
#define LUA_call(L,n,r)		LUA_callk(L, (n), (r), 0, NULL)

LUA_API int   (LUA_getctx) (LUA_State *L, int *ctx);

LUA_API int   (LUA_pcallk) (LUA_State *L, int nargs, int nresults, int errfunc,
                            int ctx, LUA_CFunction k);
#define LUA_pcall(L,n,r,f)	LUA_pcallk(L, (n), (r), (f), 0, NULL)

LUA_API int   (LUA_load) (LUA_State *L, LUA_Reader reader, void *dt,
                                        const char *chunkname,
                                        const char *mode);

LUA_API int (LUA_dump) (LUA_State *L, LUA_Writer writer, void *data);


/*
** coroutine functions
*/
LUA_API int  (LUA_yieldk) (LUA_State *L, int nresults, int ctx,
                           LUA_CFunction k);
#define LUA_yield(L,n)		LUA_yieldk(L, (n), 0, NULL)
LUA_API int  (LUA_resume) (LUA_State *L, LUA_State *from, int narg);
LUA_API int  (LUA_status) (LUA_State *L);

/*
** garbage-collection function and options
*/

#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7
#define LUA_GCSETMAJORINC	8
#define LUA_GCISRUNNING		9
#define LUA_GCGEN		10
#define LUA_GCINC		11

LUA_API int (LUA_gc) (LUA_State *L, int what, int data);


/*
** miscellaneous functions
*/

LUA_API int   (LUA_error) (LUA_State *L);

LUA_API int   (LUA_next) (LUA_State *L, int idx);

LUA_API void  (LUA_concat) (LUA_State *L, int n);
LUA_API void  (LUA_len)    (LUA_State *L, int idx);

LUA_API LUA_Alloc (LUA_getallocf) (LUA_State *L, void **ud);
LUA_API void      (LUA_setallocf) (LUA_State *L, LUA_Alloc f, void *ud);



/*
** ===============================================================
** some useful macros
** ===============================================================
*/

#define LUA_tonumber(L,i)	LUA_tonumberx(L,i,NULL)
#define LUA_tointeger(L,i)	LUA_tointegerx(L,i,NULL)
#define LUA_tounsigned(L,i)	LUA_tounsignedx(L,i,NULL)

#define LUA_pop(L,n)		LUA_settop(L, -(n)-1)

#define LUA_newtable(L)		LUA_createtable(L, 0, 0)

#define LUA_register(L,n,f) (LUA_pushcfunction(L, (f)), LUA_setglobal(L, (n)))

#define LUA_pushcfunction(L,f)	LUA_pushcclosure(L, (f), 0)

#define LUA_isfunction(L,n)	(LUA_type(L, (n)) == LUA_TFUNCTION)
#define LUA_istable(L,n)	(LUA_type(L, (n)) == LUA_TTABLE)
#define LUA_islightuserdata(L,n)	(LUA_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define LUA_isnil(L,n)		(LUA_type(L, (n)) == LUA_TNIL)
#define LUA_isboolean(L,n)	(LUA_type(L, (n)) == LUA_TBOOLEAN)
#define LUA_isthread(L,n)	(LUA_type(L, (n)) == LUA_TTHREAD)
#define LUA_isnone(L,n)		(LUA_type(L, (n)) == LUA_TNONE)
#define LUA_isnoneornil(L, n)	(LUA_type(L, (n)) <= 0)

#define LUA_pushliteral(L, s)	\
	LUA_pushlstring(L, "" s, (sizeof(s)/sizeof(char))-1)

#define LUA_pushglobaltable(L)  \
	LUA_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS)

#define LUA_tostring(L,i)	LUA_tolstring(L, (i), NULL)



/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILCALL 4


/*
** Event masks
*/
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

typedef struct LUA_Debug LUA_Debug;  /* activation record */


/* Functions to be called by the debugger in specific events */
typedef void (*LUA_Hook) (LUA_State *L, LUA_Debug *ar);


LUA_API int (LUA_getstack) (LUA_State *L, int level, LUA_Debug *ar);
LUA_API int (LUA_getinfo) (LUA_State *L, const char *what, LUA_Debug *ar);
LUA_API const char *(LUA_getlocal) (LUA_State *L, const LUA_Debug *ar, int n);
LUA_API const char *(LUA_setlocal) (LUA_State *L, const LUA_Debug *ar, int n);
LUA_API const char *(LUA_getupvalue) (LUA_State *L, int funcindex, int n);
LUA_API const char *(LUA_setupvalue) (LUA_State *L, int funcindex, int n);

LUA_API void *(LUA_upvalueid) (LUA_State *L, int fidx, int n);
LUA_API void  (LUA_upvaluejoin) (LUA_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

LUA_API int (LUA_sethook) (LUA_State *L, LUA_Hook func, int mask, int count);
LUA_API LUA_Hook (LUA_gethook) (LUA_State *L);
LUA_API int (LUA_gethookmask) (LUA_State *L);
LUA_API int (LUA_gethookcount) (LUA_State *L);


struct LUA_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */
  const char *what;	/* (S) 'LUA', 'C', 'main', 'tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  unsigned char nups;	/* (u) number of upvalues */
  unsigned char nparams;/* (u) number of parameters */
  char isvararg;        /* (u) */
  char istailcall;	/* (t) */
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  struct CallInfo *i_ci;  /* active function */
};

/* }====================================================================== */


/******************************************************************************
* Copyright (C) 1994-2013 LUA.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
