/*
** $Id: lauxlib.c,v 1.248 2013/03/21 13:54:57 roberto Exp $
** Auxiliary functions for building LUA libraries
** See Copyright Notice in LUA.h
*/


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* This file uses only the official API of LUA.
** Any function declared here could be written as an application function.
*/

#define lauxlib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"


/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	12	/* size of the first part of the stack */
#define LEVELS2	10	/* size of the second part of the stack */



/*
** search for 'objidx' in table at index -1.
** return 1 + string at top if find a good name.
*/
static int findfield (LUA_State *L, int objidx, int level) {
  if (level == 0 || !LUA_istable(L, -1))
    return 0;  /* not found */
  LUA_pushnil(L);  /* start 'next' loop */
  while (LUA_next(L, -2)) {  /* for each pair in table */
    if (LUA_type(L, -2) == LUA_TSTRING) {  /* ignore non-string keys */
      if (LUA_rawequal(L, objidx, -1)) {  /* found object? */
        LUA_pop(L, 1);  /* remove value (but keep name) */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively */
        LUA_remove(L, -2);  /* remove table (but keep name) */
        LUA_pushliteral(L, ".");
        LUA_insert(L, -2);  /* place '.' between the two names */
        LUA_concat(L, 3);
        return 1;
      }
    }
    LUA_pop(L, 1);  /* remove value */
  }
  return 0;  /* not found */
}


static int pushglobalfuncname (LUA_State *L, LUA_Debug *ar) {
  int top = LUA_gettop(L);
  LUA_getinfo(L, "f", ar);  /* push function */
  LUA_pushglobaltable(L);
  if (findfield(L, top + 1, 2)) {
    LUA_copy(L, -1, top + 1);  /* move name to proper place */
    LUA_pop(L, 2);  /* remove pushed values */
    return 1;
  }
  else {
    LUA_settop(L, top);  /* remove function and global table */
    return 0;
  }
}


static void pushfuncname (LUA_State *L, LUA_Debug *ar) {
  if (*ar->namewhat != '\0')  /* is there a name? */
    LUA_pushfstring(L, "function " LUA_QS, ar->name);
  else if (*ar->what == 'm')  /* main? */
      LUA_pushliteral(L, "main chunk");
  else if (*ar->what == 'C') {
    if (pushglobalfuncname(L, ar)) {
      LUA_pushfstring(L, "function " LUA_QS, LUA_tostring(L, -1));
      LUA_remove(L, -2);  /* remove name */
    }
    else
      LUA_pushliteral(L, "?");
  }
  else
    LUA_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
}


static int countlevels (LUA_State *L) {
  LUA_Debug ar;
  int li = 1, le = 1;
  /* find an upper bound */
  while (LUA_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search */
  while (li < le) {
    int m = (li + le)/2;
    if (LUA_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


LUALIB_API void LUAL_traceback (LUA_State *L, LUA_State *L1,
                                const char *msg, int level) {
  LUA_Debug ar;
  int top = LUA_gettop(L);
  int numlevels = countlevels(L1);
  int mark = (numlevels > LEVELS1 + LEVELS2) ? LEVELS1 : 0;
  if (msg) LUA_pushfstring(L, "%s\n", msg);
  LUA_pushliteral(L, "stack traceback:");
  while (LUA_getstack(L1, level++, &ar)) {
    if (level == mark) {  /* too many levels? */
      LUA_pushliteral(L, "\n\t...");  /* add a '...' */
      level = numlevels - LEVELS2;  /* and skip to last ones */
    }
    else {
      LUA_getinfo(L1, "Slnt", &ar);
      LUA_pushfstring(L, "\n\t%s:", ar.short_src);
      if (ar.currentline > 0)
        LUA_pushfstring(L, "%d:", ar.currentline);
      LUA_pushliteral(L, " in ");
      pushfuncname(L, &ar);
      if (ar.istailcall)
        LUA_pushliteral(L, "\n\t(...tail calls...)");
      LUA_concat(L, LUA_gettop(L) - top);
    }
  }
  LUA_concat(L, LUA_gettop(L) - top);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

LUALIB_API int LUAL_argerror (LUA_State *L, int narg, const char *extramsg) {
  LUA_Debug ar;
  if (!LUA_getstack(L, 0, &ar))  /* no stack frame? */
    return LUAL_error(L, "bad argument #%d (%s)", narg, extramsg);
  LUA_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat, "method") == 0) {
    narg--;  /* do not count `self' */
    if (narg == 0)  /* error is in the self argument itself? */
      return LUAL_error(L, "calling " LUA_QS " on bad self (%s)",
                           ar.name, extramsg);
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? LUA_tostring(L, -1) : "?";
  return LUAL_error(L, "bad argument #%d to " LUA_QS " (%s)",
                        narg, ar.name, extramsg);
}


static int typeerror (LUA_State *L, int narg, const char *tname) {
  const char *msg = LUA_pushfstring(L, "%s expected, got %s",
                                    tname, LUAL_typename(L, narg));
  return LUAL_argerror(L, narg, msg);
}


static void tag_error (LUA_State *L, int narg, int tag) {
  typeerror(L, narg, LUA_typename(L, tag));
}


LUALIB_API void LUAL_where (LUA_State *L, int level) {
  LUA_Debug ar;
  if (LUA_getstack(L, level, &ar)) {  /* check function at level */
    LUA_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      LUA_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  LUA_pushliteral(L, "");  /* else, no information available... */
}


LUALIB_API int LUAL_error (LUA_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  LUAL_where(L, 1);
  LUA_pushvfstring(L, fmt, argp);
  va_end(argp);
  LUA_concat(L, 2);
  return LUA_error(L);
}


LUALIB_API int LUAL_fileresult (LUA_State *L, int stat, const char *fname) {
  int en = errno;  /* calls to LUA API may change this value */
  if (stat) {
    LUA_pushboolean(L, 1);
    return 1;
  }
  else {
    LUA_pushnil(L);
    if (fname)
      LUA_pushfstring(L, "%s: %s", fname, strerror(en));
    else
      LUA_pushstring(L, strerror(en));
    LUA_pushinteger(L, en);
    return 3;
  }
}


#if !defined(inspectstat)	/* { */

#if defined(LUA_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define inspectstat(stat,what)  /* no op */

#endif

#endif				/* } */


LUALIB_API int LUAL_execresult (LUA_State *L, int stat) {
  const char *what = "exit";  /* type of termination */
  if (stat == -1)  /* error? */
    return LUAL_fileresult(L, 0, NULL);
  else {
    inspectstat(stat, what);  /* interpret result */
    if (*what == 'e' && stat == 0)  /* successful termination? */
      LUA_pushboolean(L, 1);
    else
      LUA_pushnil(L);
    LUA_pushstring(L, what);
    LUA_pushinteger(L, stat);
    return 3;  /* return true/nil,what,code */
  }
}

/* }====================================================== */


/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

LUALIB_API int LUAL_newmetatable (LUA_State *L, const char *tname) {
  LUAL_getmetatable(L, tname);  /* try to get metatable */
  if (!LUA_isnil(L, -1))  /* name already in use? */
    return 0;  /* leave previous value on top, but return 0 */
  LUA_pop(L, 1);
  LUA_newtable(L);  /* create metatable */
  LUA_pushvalue(L, -1);
  LUA_setfield(L, LUA_REGISTRYINDEX, tname);  /* registry.name = metatable */
  return 1;
}


LUALIB_API void LUAL_setmetatable (LUA_State *L, const char *tname) {
  LUAL_getmetatable(L, tname);
  LUA_setmetatable(L, -2);
}


LUALIB_API void *LUAL_testudata (LUA_State *L, int ud, const char *tname) {
  void *p = LUA_touserdata(L, ud);
  if (p != NULL) {  /* value is a userdata? */
    if (LUA_getmetatable(L, ud)) {  /* does it have a metatable? */
      LUAL_getmetatable(L, tname);  /* get correct metatable */
      if (!LUA_rawequal(L, -1, -2))  /* not the same? */
        p = NULL;  /* value is a userdata with wrong metatable */
      LUA_pop(L, 2);  /* remove both metatables */
      return p;
    }
  }
  return NULL;  /* value is not a userdata with a metatable */
}


LUALIB_API void *LUAL_checkudata (LUA_State *L, int ud, const char *tname) {
  void *p = LUAL_testudata(L, ud, tname);
  if (p == NULL) typeerror(L, ud, tname);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

LUALIB_API int LUAL_checkoption (LUA_State *L, int narg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? LUAL_optstring(L, narg, def) :
                             LUAL_checkstring(L, narg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return LUAL_argerror(L, narg,
                       LUA_pushfstring(L, "invalid option " LUA_QS, name));
}


LUALIB_API void LUAL_checkstack (LUA_State *L, int space, const char *msg) {
  /* keep some extra space to run error routines, if needed */
  const int extra = LUA_MINSTACK;
  if (!LUA_checkstack(L, space + extra)) {
    if (msg)
      LUAL_error(L, "stack overflow (%s)", msg);
    else
      LUAL_error(L, "stack overflow");
  }
}


LUALIB_API void LUAL_checktype (LUA_State *L, int narg, int t) {
  if (LUA_type(L, narg) != t)
    tag_error(L, narg, t);
}


LUALIB_API void LUAL_checkany (LUA_State *L, int narg) {
  if (LUA_type(L, narg) == LUA_TNONE)
    LUAL_argerror(L, narg, "value expected");
}


LUALIB_API const char *LUAL_checklstring (LUA_State *L, int narg, size_t *len) {
  const char *s = LUA_tolstring(L, narg, len);
  if (!s) tag_error(L, narg, LUA_TSTRING);
  return s;
}


LUALIB_API const char *LUAL_optlstring (LUA_State *L, int narg,
                                        const char *def, size_t *len) {
  if (LUA_isnoneornil(L, narg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return LUAL_checklstring(L, narg, len);
}


LUALIB_API LUA_Number LUAL_checknumber (LUA_State *L, int narg) {
  int isnum;
  LUA_Number d = LUA_tonumberx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


LUALIB_API LUA_Number LUAL_optnumber (LUA_State *L, int narg, LUA_Number def) {
  return LUAL_opt(L, LUAL_checknumber, narg, def);
}


LUALIB_API LUA_Integer LUAL_checkinteger (LUA_State *L, int narg) {
  int isnum;
  LUA_Integer d = LUA_tointegerx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


LUALIB_API LUA_Unsigned LUAL_checkunsigned (LUA_State *L, int narg) {
  int isnum;
  LUA_Unsigned d = LUA_tounsignedx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


LUALIB_API LUA_Integer LUAL_optinteger (LUA_State *L, int narg,
                                                      LUA_Integer def) {
  return LUAL_opt(L, LUAL_checkinteger, narg, def);
}


LUALIB_API LUA_Unsigned LUAL_optunsigned (LUA_State *L, int narg,
                                                        LUA_Unsigned def) {
  return LUAL_opt(L, LUAL_checkunsigned, narg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
#define buffonstack(B)	((B)->b != (B)->initb)


/*
** returns a pointer to a free area with at least 'sz' bytes
*/
LUALIB_API char *LUAL_prepbuffsize (LUAL_Buffer *B, size_t sz) {
  LUA_State *L = B->L;
  if (B->size - B->n < sz) {  /* not enough space? */
    char *newbuff;
    size_t newsize = B->size * 2;  /* double buffer size */
    if (newsize - B->n < sz)  /* not big enough? */
      newsize = B->n + sz;
    if (newsize < B->n || newsize - B->n < sz)
      LUAL_error(L, "buffer too large");
    /* create larger buffer */
    newbuff = (char *)LUA_newuserdata(L, newsize * sizeof(char));
    /* move content to new buffer */
    memcpy(newbuff, B->b, B->n * sizeof(char));
    if (buffonstack(B))
      LUA_remove(L, -2);  /* remove old buffer */
    B->b = newbuff;
    B->size = newsize;
  }
  return &B->b[B->n];
}


LUALIB_API void LUAL_addlstring (LUAL_Buffer *B, const char *s, size_t l) {
  char *b = LUAL_prepbuffsize(B, l);
  memcpy(b, s, l * sizeof(char));
  LUAL_addsize(B, l);
}


LUALIB_API void LUAL_addstring (LUAL_Buffer *B, const char *s) {
  LUAL_addlstring(B, s, strlen(s));
}


LUALIB_API void LUAL_pushresult (LUAL_Buffer *B) {
  LUA_State *L = B->L;
  LUA_pushlstring(L, B->b, B->n);
  if (buffonstack(B))
    LUA_remove(L, -2);  /* remove old buffer */
}


LUALIB_API void LUAL_pushresultsize (LUAL_Buffer *B, size_t sz) {
  LUAL_addsize(B, sz);
  LUAL_pushresult(B);
}


LUALIB_API void LUAL_addvalue (LUAL_Buffer *B) {
  LUA_State *L = B->L;
  size_t l;
  const char *s = LUA_tolstring(L, -1, &l);
  if (buffonstack(B))
    LUA_insert(L, -2);  /* put value below buffer */
  LUAL_addlstring(B, s, l);
  LUA_remove(L, (buffonstack(B)) ? -2 : -1);  /* remove value */
}


LUALIB_API void LUAL_buffinit (LUA_State *L, LUAL_Buffer *B) {
  B->L = L;
  B->b = B->initb;
  B->n = 0;
  B->size = LUAL_BUFFERSIZE;
}


LUALIB_API char *LUAL_buffinitsize (LUA_State *L, LUAL_Buffer *B, size_t sz) {
  LUAL_buffinit(L, B);
  return LUAL_prepbuffsize(B, sz);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/* index of free-list header */
#define freelist	0


LUALIB_API int LUAL_ref (LUA_State *L, int t) {
  int ref;
  if (LUA_isnil(L, -1)) {
    LUA_pop(L, 1);  /* remove from stack */
    return LUA_REFNIL;  /* `nil' has a unique fixed reference */
  }
  t = LUA_absindex(L, t);
  LUA_rawgeti(L, t, freelist);  /* get first free element */
  ref = (int)LUA_tointeger(L, -1);  /* ref = t[freelist] */
  LUA_pop(L, 1);  /* remove it from stack */
  if (ref != 0) {  /* any free element? */
    LUA_rawgeti(L, t, ref);  /* remove it from list */
    LUA_rawseti(L, t, freelist);  /* (t[freelist] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)LUA_rawlen(L, t) + 1;  /* get a new reference */
  LUA_rawseti(L, t, ref);
  return ref;
}


LUALIB_API void LUAL_unref (LUA_State *L, int t, int ref) {
  if (ref >= 0) {
    t = LUA_absindex(L, t);
    LUA_rawgeti(L, t, freelist);
    LUA_rawseti(L, t, ref);  /* t[ref] = t[freelist] */
    LUA_pushinteger(L, ref);
    LUA_rawseti(L, t, freelist);  /* t[freelist] = ref */
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  int n;  /* number of pre-read characters */
  FILE *f;  /* file being read */
  char buff[LUAL_BUFFERSIZE];  /* area for reading file */
} LoadF;


static const char *getF (LUA_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;  /* not used */
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  }
  else {  /* read a block from file */
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}


static int errfile (LUA_State *L, const char *what, int fnameindex) {
  const char *serr = strerror(errno);
  const char *filename = LUA_tostring(L, fnameindex) + 1;
  LUA_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
  LUA_remove(L, fnameindex);
  return LUA_ERRFILE;
}


static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* Utf8 BOM mark */
  int c;
  lf->n = 0;
  do {
    c = getc(lf->f);
    if (c == EOF || c != *(const unsigned char *)p++) return c;
    lf->buff[lf->n++] = c;  /* to be read by the parser */
  } while (*p != '\0');
  lf->n = 0;  /* prefix matched; discard it */
  return getc(lf->f);  /* return next character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment (LoadF *lf, int *cp) {
  int c = *cp = skipBOM(lf);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(lf->f);
    } while (c != EOF && c != '\n') ;
    *cp = getc(lf->f);  /* skip end-of-line, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}


LUALIB_API int LUAL_loadfilex (LUA_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = LUA_gettop(L) + 1;  /* index of filename on the stack */
  if (filename == NULL) {
    LUA_pushliteral(L, "=STDIN");
    lf.f = stdin;
  }
  else {
    LUA_pushfstring(L, "@%s", filename);
    lf.f = fopen(filename, "r");
    if (lf.f == NULL) return errfile(L, "open", fnameindex);
  }
  if (skipcomment(&lf, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add line to correct line numbers */
  if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
    lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
    if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
    skipcomment(&lf, &c);  /* re-read initial portion */
  }
  if (c != EOF)
    lf.buff[lf.n++] = c;  /* 'c' is the first character of the stream */
  status = LUA_load(L, getF, &lf, LUA_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    LUA_settop(L, fnameindex);  /* ignore results from `LUA_load' */
    return errfile(L, "read", fnameindex);
  }
  LUA_remove(L, fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (LUA_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}


LUALIB_API int LUAL_loadbufferx (LUA_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return LUA_load(L, getS, &ls, name, mode);
}


LUALIB_API int LUAL_loadstring (LUA_State *L, const char *s) {
  return LUAL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */



LUALIB_API int LUAL_getmetafield (LUA_State *L, int obj, const char *event) {
  if (!LUA_getmetatable(L, obj))  /* no metatable? */
    return 0;
  LUA_pushstring(L, event);
  LUA_rawget(L, -2);
  if (LUA_isnil(L, -1)) {
    LUA_pop(L, 2);  /* remove metatable and metafield */
    return 0;
  }
  else {
    LUA_remove(L, -2);  /* remove only metatable */
    return 1;
  }
}


LUALIB_API int LUAL_callmeta (LUA_State *L, int obj, const char *event) {
  obj = LUA_absindex(L, obj);
  if (!LUAL_getmetafield(L, obj, event))  /* no metafield? */
    return 0;
  LUA_pushvalue(L, obj);
  LUA_call(L, 1, 1);
  return 1;
}


LUALIB_API int LUAL_len (LUA_State *L, int idx) {
  int l;
  int isnum;
  LUA_len(L, idx);
  l = (int)LUA_tointegerx(L, -1, &isnum);
  if (!isnum)
    LUAL_error(L, "object length is not a number");
  LUA_pop(L, 1);  /* remove object */
  return l;
}


LUALIB_API const char *LUAL_tolstring (LUA_State *L, int idx, size_t *len) {
  if (!LUAL_callmeta(L, idx, "__tostring")) {  /* no metafield? */
    switch (LUA_type(L, idx)) {
      case LUA_TNUMBER:
      case LUA_TSTRING:
        LUA_pushvalue(L, idx);
        break;
      case LUA_TBOOLEAN:
        LUA_pushstring(L, (LUA_toboolean(L, idx) ? "TRUE" : "FALSE"));
        break;
      case LUA_TNIL:
        LUA_pushliteral(L, "NIL");
        break;
      default:
        LUA_pushfstring(L, "%s: %p", LUAL_typename(L, idx),
                                            LUA_topointer(L, idx));
        break;
    }
  }
  return LUA_tolstring(L, -1, len);
}


/*
** {======================================================
** Compatibility with 5.1 module functions
** =======================================================
*/
#if defined(LUA_COMPAT_MODULE)

static const char *LUAL_findtable (LUA_State *L, int idx,
                                   const char *fname, int szhint) {
  const char *e;
  if (idx) LUA_pushvalue(L, idx);
  do {
    e = strchr(fname, '.');
    if (e == NULL) e = fname + strlen(fname);
    LUA_pushlstring(L, fname, e - fname);
    LUA_rawget(L, -2);
    if (LUA_isnil(L, -1)) {  /* no such field? */
      LUA_pop(L, 1);  /* remove this nil */
      LUA_createtable(L, 0, (*e == '.' ? 1 : szhint)); /* new table for field */
      LUA_pushlstring(L, fname, e - fname);
      LUA_pushvalue(L, -2);
      LUA_settable(L, -4);  /* set new table into field */
    }
    else if (!LUA_istable(L, -1)) {  /* field has a non-table value? */
      LUA_pop(L, 2);  /* remove table and value */
      return fname;  /* return problematic part of the name */
    }
    LUA_remove(L, -2);  /* remove previous table */
    fname = e + 1;
  } while (*e == '.');
  return NULL;
}


/*
** Count number of elements in a LUAL_Reg list.
*/
static int libsize (const LUAL_Reg *l) {
  int size = 0;
  for (; l && l->name; l++) size++;
  return size;
}


/*
** Find or create a module table with a given name. The function
** first looks at the _LOADED table and, if that fails, try a
** global variable with that name. In any case, leaves on the stack
** the module table.
*/
LUALIB_API void LUAL_pushmodule (LUA_State *L, const char *modname,
                                 int sizehint) {
  LUAL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 1);  /* get _LOADED table */
  LUA_getfield(L, -1, modname);  /* get _LOADED[modname] */
  if (!LUA_istable(L, -1)) {  /* not found? */
    LUA_pop(L, 1);  /* remove previous result */
    /* try global variable (and create one if it does not exist) */
    LUA_pushglobaltable(L);
    if (LUAL_findtable(L, 0, modname, sizehint) != NULL)
      LUAL_error(L, "name conflict for module " LUA_QS, modname);
    LUA_pushvalue(L, -1);
    LUA_setfield(L, -3, modname);  /* _LOADED[modname] = new table */
  }
  LUA_remove(L, -2);  /* remove _LOADED table */
}


LUALIB_API void LUAL_openlib (LUA_State *L, const char *libname,
                               const LUAL_Reg *l, int nup) {
  LUAL_checkversion(L);
  if (libname) {
    LUAL_pushmodule(L, libname, libsize(l));  /* get/create library table */
    LUA_insert(L, -(nup + 1));  /* move library table to below upvalues */
  }
  if (l)
    LUAL_setfuncs(L, l, nup);
  else
    LUA_pop(L, nup);  /* remove upvalues */
}

#endif
/* }====================================================== */

/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
LUALIB_API void LUAL_setfuncs (LUA_State *L, const LUAL_Reg *l, int nup) {
  LUAL_checkversion(L);
  LUAL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    for (i = 0; i < nup; i++)  /* copy upvalues to the top */
      LUA_pushvalue(L, -nup);
    LUA_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    LUA_setfield(L, -(nup + 2), l->name);
  }
  LUA_pop(L, nup);  /* remove upvalues */
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
LUALIB_API int LUAL_getsubtable (LUA_State *L, int idx, const char *fname) {
  LUA_getfield(L, idx, fname);
  if (LUA_istable(L, -1)) return 1;  /* table already there */
  else {
    LUA_pop(L, 1);  /* remove previous result */
    idx = LUA_absindex(L, idx);
    LUA_newtable(L);
    LUA_pushvalue(L, -1);  /* copy to be left at top */
    LUA_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


/*
** stripped-down 'require'. Calls 'openf' to open a module,
** registers the result in 'package.loaded' table and, if 'glb'
** is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
LUALIB_API void LUAL_requiref (LUA_State *L, const char *modname,
                               LUA_CFunction openf, int glb) {
  LUA_pushcfunction(L, openf);
  LUA_pushstring(L, modname);  /* argument to open function */
  LUA_call(L, 1, 1);  /* open module */
  LUAL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
  LUA_pushvalue(L, -2);  /* make copy of module (call result) */
  LUA_setfield(L, -2, modname);  /* _LOADED[modname] = module */
  LUA_pop(L, 1);  /* remove _LOADED table */
  if (glb) {
    LUA_pushvalue(L, -1);  /* copy of 'mod' */
    LUA_setglobal(L, modname);  /* _G[modname] = module */
  }
}


LUALIB_API const char *LUAL_gsub (LUA_State *L, const char *s, const char *p,
                                                               const char *r) {
  const char *wild;
  size_t l = strlen(p);
  LUAL_Buffer b;
  LUAL_buffinit(L, &b);
  while ((wild = strstr(s, p)) != NULL) {
    LUAL_addlstring(&b, s, wild - s);  /* push prefix */
    LUAL_addstring(&b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after `p' */
  }
  LUAL_addstring(&b, s);  /* push last suffix */
  LUAL_pushresult(&b);
  return LUA_tostring(L, -1);
}


static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}


static int panic (LUA_State *L) {
  LUAi_writestringerror("PANIC: unprotected error in call to LUA API (%s)\n",
                   LUA_tostring(L, -1));
  return 0;  /* return to LUA to abort */
}


LUALIB_API LUA_State *LUAL_newstate (void) {
  LUA_State *L = LUA_newstate(l_alloc, NULL);
  if (L) LUA_atpanic(L, &panic);
  return L;
}


LUALIB_API void LUAL_checkversion_ (LUA_State *L, LUA_Number ver) {
  const LUA_Number *v = LUA_version(L);
  if (v != LUA_version(NULL))
    LUAL_error(L, "multiple LUA VMs detected");
  else if (*v != ver)
    LUAL_error(L, "version mismatch: app. needs %f, LUA core provides %f",
                  ver, *v);
  /* check conversions number -> integer types */
  LUA_pushnumber(L, -(LUA_Number)0x1234);
  if (LUA_tointeger(L, -1) != -0x1234 ||
      LUA_tounsigned(L, -1) != (LUA_Unsigned)-0x1234)
    LUAL_error(L, "bad conversion number->int;"
                  " must recompile LUA with proper settings");
  LUA_pop(L, 1);
}

