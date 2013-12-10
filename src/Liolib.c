/*
** $Id: liolib.c,v 2.111 2013/03/21 13:57:27 roberto Exp $
** Standard I/O (and system) library
** See Copyright Notice in LUA.h
*/


/*
** POSIX idiosyncrasy!
** This definition must come before the inclusion of 'stdio.h'; it
** should not affect non-POSIX systems
*/
#if !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define liolib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


#if !defined(LUA_checkmode)

/*
** Check whether 'mode' matches '[rwa]%+?b?'.
** Change this macro to accept other modes for 'fopen' besides
** the standard ones.
*/
#define LUA_checkmode(mode) \
	(*mode != '\0' && strchr("rwa", *(mode++)) != NULL &&	\
	(*mode != '+' || ++mode) &&  /* skip if char is '+' */	\
	(*mode != 'b' || ++mode) &&  /* skip if char is 'b' */	\
	(*mode == '\0'))

#endif

/*
** {======================================================
** LUA_popen spawns a new process connected to the current
** one through the file streams.
** =======================================================
*/

#if !defined(LUA_popen)	/* { */

#if defined(LUA_USE_POPEN)	/* { */

#define LUA_popen(L,c,m)	((void)L, fflush(NULL), popen(c,m))
#define LUA_pclose(L,file)	((void)L, pclose(file))

#elif defined(LUA_WIN)		/* }{ */

#define LUA_popen(L,c,m)		((void)L, _popen(c,m))
#define LUA_pclose(L,file)		((void)L, _pclose(file))


#else				/* }{ */

#define LUA_popen(L,c,m)		((void)((void)c, m),  \
		LUAL_error(L, LUA_QL("POPEN") " not supported"), (FILE*)0)
#define LUA_pclose(L,file)		((void)((void)L, file), -1)


#endif				/* } */

#endif			/* } */

/* }====================================================== */


/*
** {======================================================
** LUA_fseek/LUA_ftell: configuration for longer offsets
** =======================================================
*/

#if !defined(LUA_fseek)	/* { */

#if defined(LUA_USE_POSIX)

#define l_fseek(f,o,w)		fseeko(f,o,w)
#define l_ftell(f)		ftello(f)
#define l_seeknum		off_t

#elif defined(LUA_WIN) && !defined(_CRTIMP_TYPEINFO) \
   && defined(_MSC_VER) && (_MSC_VER >= 1400)
/* Windows (but not DDK) and Visual C++ 2005 or higher */

#define l_fseek(f,o,w)		_fseeki64(f,o,w)
#define l_ftell(f)		_ftelli64(f)
#define l_seeknum		__int64

#else

#define l_fseek(f,o,w)		fseek(f,o,w)
#define l_ftell(f)		ftell(f)
#define l_seeknum		long

#endif

#endif			/* } */

/* }====================================================== */


#define IO_PREFIX	"_IO_"
#define IO_INPUT	(IO_PREFIX "INPUT")
#define IO_OUTPUT	(IO_PREFIX "OUTPUT")


typedef LUAL_Stream LStream;


#define tolstream(L)	((LStream *)LUAL_checkudata(L, 1, LUA_FILEHANDLE))

#define isclosed(p)	((p)->closef == NULL)


static int io_type (LUA_State *L) {
  LStream *p;
  LUAL_checkany(L, 1);
  p = (LStream *)LUAL_testudata(L, 1, LUA_FILEHANDLE);
  if (p == NULL)
    LUA_pushnil(L);  /* not a file */
  else if (isclosed(p))
    LUA_pushliteral(L, "closed file");
  else
    LUA_pushliteral(L, "file");
  return 1;
}


static int f_tostring (LUA_State *L) {
  LStream *p = tolstream(L);
  if (isclosed(p))
    LUA_pushliteral(L, "file (closed)");
  else
    LUA_pushfstring(L, "file (%p)", p->f);
  return 1;
}


static FILE *tofile (LUA_State *L) {
  LStream *p = tolstream(L);
  if (isclosed(p))
    LUAL_error(L, "attempt to use a closed file");
  LUA_assert(p->f);
  return p->f;
}


/*
** When creating file handles, always creates a `closed' file handle
** before opening the actual file; so, if there is a memory error, the
** file is not left opened.
*/
static LStream *newprefile (LUA_State *L) {
  LStream *p = (LStream *)LUA_newuserdata(L, sizeof(LStream));
  p->closef = NULL;  /* mark file handle as 'closed' */
  LUAL_setmetatable(L, LUA_FILEHANDLE);
  return p;
}


static int aux_close (LUA_State *L) {
  LStream *p = tolstream(L);
  LUA_CFunction cf = p->closef;
  p->closef = NULL;  /* mark stream as closed */
  return (*cf)(L);  /* close it */
}


static int io_close (LUA_State *L) {
  if (LUA_isnone(L, 1))  /* no argument? */
    LUA_getfield(L, LUA_REGISTRYINDEX, IO_OUTPUT);  /* use standard output */
  tofile(L);  /* make sure argument is an open stream */
  return aux_close(L);
}


static int f_gc (LUA_State *L) {
  LStream *p = tolstream(L);
  if (!isclosed(p) && p->f != NULL)
    aux_close(L);  /* ignore closed and incompletely open files */
  return 0;
}


/*
** function to close regular files
*/
static int io_fclose (LUA_State *L) {
  LStream *p = tolstream(L);
  int res = fclose(p->f);
  return LUAL_fileresult(L, (res == 0), NULL);
}


static LStream *newfile (LUA_State *L) {
  LStream *p = newprefile(L);
  p->f = NULL;
  p->closef = &io_fclose;
  return p;
}


static void opencheck (LUA_State *L, const char *fname, const char *mode) {
  LStream *p = newfile(L);
  p->f = fopen(fname, mode);
  if (p->f == NULL)
    LUAL_error(L, "cannot open file " LUA_QS " (%s)", fname, strerror(errno));
}


static int io_open (LUA_State *L) {
  const char *filename = LUAL_checkstring(L, 1);
  const char *mode = LUAL_optstring(L, 2, "r");
  LStream *p = newfile(L);
  const char *md = mode;  /* to traverse/check mode */
  LUAL_argcheck(L, LUA_checkmode(md), 2, "invalid mode");
  p->f = fopen(filename, mode);
  return (p->f == NULL) ? LUAL_fileresult(L, 0, filename) : 1;
}


/*
** function to close 'popen' files
*/
static int io_pclose (LUA_State *L) {
  LStream *p = tolstream(L);
  return LUAL_execresult(L, LUA_pclose(L, p->f));
}


static int io_popen (LUA_State *L) {
  const char *filename = LUAL_checkstring(L, 1);
  const char *mode = LUAL_optstring(L, 2, "r");
  LStream *p = newprefile(L);
  p->f = LUA_popen(L, filename, mode);
  p->closef = &io_pclose;
  return (p->f == NULL) ? LUAL_fileresult(L, 0, filename) : 1;
}


static int io_tmpfile (LUA_State *L) {
  LStream *p = newfile(L);
  p->f = tmpfile();
  return (p->f == NULL) ? LUAL_fileresult(L, 0, NULL) : 1;
}


static FILE *getiofile (LUA_State *L, const char *findex) {
  LStream *p;
  LUA_getfield(L, LUA_REGISTRYINDEX, findex);
  p = (LStream *)LUA_touserdata(L, -1);
  if (isclosed(p))
    LUAL_error(L, "standard %s file is closed", findex + strlen(IO_PREFIX));
  return p->f;
}


static int g_iofile (LUA_State *L, const char *f, const char *mode) {
  if (!LUA_isnoneornil(L, 1)) {
    const char *filename = LUA_tostring(L, 1);
    if (filename)
      opencheck(L, filename, mode);
    else {
      tofile(L);  /* check that it's a valid file handle */
      LUA_pushvalue(L, 1);
    }
    LUA_setfield(L, LUA_REGISTRYINDEX, f);
  }
  /* return current value */
  LUA_getfield(L, LUA_REGISTRYINDEX, f);
  return 1;
}


static int io_input (LUA_State *L) {
  return g_iofile(L, IO_INPUT, "r");
}


static int io_output (LUA_State *L) {
  return g_iofile(L, IO_OUTPUT, "w");
}


static int io_readline (LUA_State *L);


static void aux_lines (LUA_State *L, int toclose) {
  int i;
  int n = LUA_gettop(L) - 1;  /* number of arguments to read */
  /* ensure that arguments will fit here and into 'io_readline' stack */
  LUAL_argcheck(L, n <= LUA_MINSTACK - 3, LUA_MINSTACK - 3, "too many options");
  LUA_pushvalue(L, 1);  /* file handle */
  LUA_pushinteger(L, n);  /* number of arguments to read */
  LUA_pushboolean(L, toclose);  /* close/not close file when finished */
  for (i = 1; i <= n; i++) LUA_pushvalue(L, i + 1);  /* copy arguments */
  LUA_pushcclosure(L, io_readline, 3 + n);
}


static int f_lines (LUA_State *L) {
  tofile(L);  /* check that it's a valid file handle */
  aux_lines(L, 0);
  return 1;
}


static int io_lines (LUA_State *L) {
  int toclose;
  if (LUA_isnone(L, 1)) LUA_pushnil(L);  /* at least one argument */
  if (LUA_isnil(L, 1)) {  /* no file name? */
    LUA_getfield(L, LUA_REGISTRYINDEX, IO_INPUT);  /* get default input */
    LUA_replace(L, 1);  /* put it at index 1 */
    tofile(L);  /* check that it's a valid file handle */
    toclose = 0;  /* do not close it after iteration */
  }
  else {  /* open a new file */
    const char *filename = LUAL_checkstring(L, 1);
    opencheck(L, filename, "r");
    LUA_replace(L, 1);  /* put file at index 1 */
    toclose = 1;  /* close it after iteration */
  }
  aux_lines(L, toclose);
  return 1;
}


/*
** {======================================================
** READ
** =======================================================
*/


static int read_number (LUA_State *L, FILE *f) {
  LUA_Number d;
  if (fscanf(f, LUA_NUMBER_SCAN, &d) == 1) {
    LUA_pushnumber(L, d);
    return 1;
  }
  else {
   LUA_pushnil(L);  /* "result" to be removed */
   return 0;  /* read fails */
  }
}


static int test_eof (LUA_State *L, FILE *f) {
  int c = getc(f);
  ungetc(c, f);
  LUA_pushlstring(L, NULL, 0);
  return (c != EOF);
}


static int read_line (LUA_State *L, FILE *f, int chop) {
  LUAL_Buffer b;
  LUAL_buffinit(L, &b);
  for (;;) {
    size_t l;
    char *p = LUAL_prepbuffer(&b);
    if (fgets(p, LUAL_BUFFERSIZE, f) == NULL) {  /* eof? */
      LUAL_pushresult(&b);  /* close buffer */
      return (LUA_rawlen(L, -1) > 0);  /* check whether read something */
    }
    l = strlen(p);
    if (l == 0 || p[l-1] != '\n')
      LUAL_addsize(&b, l);
    else {
      LUAL_addsize(&b, l - chop);  /* chop 'eol' if needed */
      LUAL_pushresult(&b);  /* close buffer */
      return 1;  /* read at least an `eol' */
    }
  }
}


#define MAX_SIZE_T	(~(size_t)0)

static void read_all (LUA_State *L, FILE *f) {
  size_t rlen = LUAL_BUFFERSIZE;  /* how much to read in each cycle */
  LUAL_Buffer b;
  LUAL_buffinit(L, &b);
  for (;;) {
    char *p = LUAL_prepbuffsize(&b, rlen);
    size_t nr = fread(p, sizeof(char), rlen, f);
    LUAL_addsize(&b, nr);
    if (nr < rlen) break;  /* eof? */
    else if (rlen <= (MAX_SIZE_T / 4))  /* avoid buffers too large */
      rlen *= 2;  /* double buffer size at each iteration */
  }
  LUAL_pushresult(&b);  /* close buffer */
}


static int read_chars (LUA_State *L, FILE *f, size_t n) {
  size_t nr;  /* number of chars actually read */
  char *p;
  LUAL_Buffer b;
  LUAL_buffinit(L, &b);
  p = LUAL_prepbuffsize(&b, n);  /* prepare buffer to read whole block */
  nr = fread(p, sizeof(char), n, f);  /* try to read 'n' chars */
  LUAL_addsize(&b, nr);
  LUAL_pushresult(&b);  /* close buffer */
  return (nr > 0);  /* true iff read something */
}


static int g_read (LUA_State *L, FILE *f, int first) {
  int nargs = LUA_gettop(L) - 1;
  int success;
  int n;
  clearerr(f);
  if (nargs == 0) {  /* no arguments? */
    success = read_line(L, f, 1);
    n = first+1;  /* to return 1 result */
  }
  else {  /* ensure stack space for all results and for auxlib's buffer */
    LUAL_checkstack(L, nargs+LUA_MINSTACK, "too many arguments");
    success = 1;
    for (n = first; nargs-- && success; n++) {
      if (LUA_type(L, n) == LUA_TNUMBER) {
        size_t l = (size_t)LUA_tointeger(L, n);
        success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
      }
      else {
        const char *p = LUA_tostring(L, n);
        LUAL_argcheck(L, p && p[0] == '*', n, "invalid option");
        switch (p[1]) {
          case 'n':  /* number */
            success = read_number(L, f);
            break;
          case 'l':  /* line */
            success = read_line(L, f, 1);
            break;
          case 'L':  /* line with end-of-line */
            success = read_line(L, f, 0);
            break;
          case 'a':  /* file */
            read_all(L, f);  /* read entire file */
            success = 1; /* always success */
            break;
          default:
            return LUAL_argerror(L, n, "invalid format");
        }
      }
    }
  }
  if (ferror(f))
    return LUAL_fileresult(L, 0, NULL);
  if (!success) {
    LUA_pop(L, 1);  /* remove last result */
    LUA_pushnil(L);  /* push nil instead */
  }
  return n - first;
}


static int io_read (LUA_State *L) {
  return g_read(L, getiofile(L, IO_INPUT), 1);
}


static int f_read (LUA_State *L) {
  return g_read(L, tofile(L), 2);
}


static int io_readline (LUA_State *L) {
  LStream *p = (LStream *)LUA_touserdata(L, LUA_upvalueindex(1));
  int i;
  int n = (int)LUA_tointeger(L, LUA_upvalueindex(2));
  if (isclosed(p))  /* file is already closed? */
    return LUAL_error(L, "file is already closed");
  LUA_settop(L , 1);
  for (i = 1; i <= n; i++)  /* push arguments to 'g_read' */
    LUA_pushvalue(L, LUA_upvalueindex(3 + i));
  n = g_read(L, p->f, 2);  /* 'n' is number of results */
  LUA_assert(n > 0);  /* should return at least a nil */
  if (!LUA_isnil(L, -n))  /* read at least one value? */
    return n;  /* return them */
  else {  /* first result is nil: EOF or error */
    if (n > 1) {  /* is there error information? */
      /* 2nd result is error message */
      return LUAL_error(L, "%s", LUA_tostring(L, -n + 1));
    }
    if (LUA_toboolean(L, LUA_upvalueindex(3))) {  /* generator created file? */
      LUA_settop(L, 0);
      LUA_pushvalue(L, LUA_upvalueindex(1));
      aux_close(L);  /* close it */
    }
    return 0;
  }
}

/* }====================================================== */


static int g_write (LUA_State *L, FILE *f, int arg) {
  int nargs = LUA_gettop(L) - arg;
  int status = 1;
  for (; nargs--; arg++) {
    if (LUA_type(L, arg) == LUA_TNUMBER) {
      /* optimization: could be done exactly as for strings */
      status = status &&
          fprintf(f, LUA_NUMBER_FMT, LUA_tonumber(L, arg)) > 0;
    }
    else {
      size_t l;
      const char *s = LUAL_checklstring(L, arg, &l);
      status = status && (fwrite(s, sizeof(char), l, f) == l);
    }
  }
  if (status) return 1;  /* file handle already on stack top */
  else return LUAL_fileresult(L, status, NULL);
}


static int io_write (LUA_State *L) {
  return g_write(L, getiofile(L, IO_OUTPUT), 1);
}


static int f_write (LUA_State *L) {
  FILE *f = tofile(L);
  LUA_pushvalue(L, 1);  /* push file at the stack top (to be returned) */
  return g_write(L, f, 2);
}


static int f_seek (LUA_State *L) {
  static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  static const char *const modenames[] = {"SET", "CUR", "END", NULL};
  FILE *f = tofile(L);
  int op = LUAL_checkoption(L, 2, "CUR", modenames);
  LUA_Number p3 = LUAL_optnumber(L, 3, 0);
  l_seeknum offset = (l_seeknum)p3;
  LUAL_argcheck(L, (LUA_Number)offset == p3, 3,
                  "not an integer in proper range");
  op = l_fseek(f, offset, mode[op]);
  if (op)
    return LUAL_fileresult(L, 0, NULL);  /* error */
  else {
    LUA_pushnumber(L, (LUA_Number)l_ftell(f));
    return 1;
  }
}


static int f_setvbuf (LUA_State *L) {
  static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
  static const char *const modenames[] = {"NO", "FULL", "LINE", NULL};
  FILE *f = tofile(L);
  int op = LUAL_checkoption(L, 2, NULL, modenames);
  LUA_Integer sz = LUAL_optinteger(L, 3, LUAL_BUFFERSIZE);
  int res = setvbuf(f, NULL, mode[op], sz);
  return LUAL_fileresult(L, res == 0, NULL);
}



static int io_flush (LUA_State *L) {
  return LUAL_fileresult(L, fflush(getiofile(L, IO_OUTPUT)) == 0, NULL);
}


static int f_flush (LUA_State *L) {
  return LUAL_fileresult(L, fflush(tofile(L)) == 0, NULL);
}


/*
** functions for 'io' library
*/
static const LUAL_Reg iolib[] = {
  {"CLOSE", io_close},
  {"FLUSH", io_flush},
  {"INPUT", io_input},
  {"LINES", io_lines},
  {"OPEN", io_open},
  {"OUTPUT", io_output},
  {"POPEN", io_popen},
  {"READ", io_read},
  {"TMPFILE", io_tmpfile},
  {"TYPE", io_type},
  {"WRITE", io_write},
  {NULL, NULL}
};


/*
** methods for file handles
*/
static const LUAL_Reg flib[] = {
  {"CLOSE", io_close},
  {"FLUSH", f_flush},
  {"LINES", f_lines},
  {"READ", f_read},
  {"SEEK", f_seek},
  {"SETVBUF", f_setvbuf},
  {"WRITE", f_write},
  {"__GC", f_gc},
  {"__TOSTRING", f_tostring},
  {NULL, NULL}
};


static void createmeta (LUA_State *L) {
  LUAL_newmetatable(L, LUA_FILEHANDLE);  /* create metatable for file handles */
  LUA_pushvalue(L, -1);  /* push metatable */
  LUA_setfield(L, -2, "__INDEX");  /* metatable.__index = metatable */
  LUAL_setfuncs(L, flib, 0);  /* add file methods to new metatable */
  LUA_pop(L, 1);  /* pop new metatable */
}


/*
** function to (not) close the standard files stdin, stdout, and stderr
*/
static int io_noclose (LUA_State *L) {
  LStream *p = tolstream(L);
  p->closef = &io_noclose;  /* keep file opened */
  LUA_pushnil(L);
  LUA_pushliteral(L, "cannot close standard file");
  return 2;
}


static void createstdfile (LUA_State *L, FILE *f, const char *k,
                           const char *fname) {
  LStream *p = newprefile(L);
  p->f = f;
  p->closef = &io_noclose;
  if (k != NULL) {
    LUA_pushvalue(L, -1);
    LUA_setfield(L, LUA_REGISTRYINDEX, k);  /* add file to registry */
  }
  LUA_setfield(L, -2, fname);  /* add file to module */
}


LUAMOD_API int LUAopen_io (LUA_State *L) {
  LUAL_newlib(L, iolib);  /* new module */
  createmeta(L);
  /* create (and set) default files */
  createstdfile(L, stdin, IO_INPUT, "STDIN");
  createstdfile(L, stdout, IO_OUTPUT, "STDOUT");
  createstdfile(L, stderr, NULL, "STDERR");
  return 1;
}

