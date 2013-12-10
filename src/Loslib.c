/*
** $Id: loslib.c,v 1.40 2012/10/19 15:54:02 roberto Exp $
** Standard Operating System library
** See Copyright Notice in LUA.h
*/


#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define loslib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


/*
** list of valid conversion specifiers for the 'strftime' function
*/
#if !defined(LUA_STRFTIMEOPTIONS)

#if !defined(LUA_USE_POSIX)
#define LUA_STRFTIMEOPTIONS	{ "aAbBcdHIjmMpSUwWxXyYz%", "" }
#else
#define LUA_STRFTIMEOPTIONS \
	{ "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%", "" \
	  "", "E", "cCxXyY",  \
	  "O", "deHImMSuUVwWy" }
#endif

#endif



/*
** By default, LUA uses tmpnam except when POSIX is available, where it
** uses mkstemp.
*/
#if defined(LUA_USE_MKSTEMP)
#include <unistd.h>
#define LUA_TMPNAMBUFSIZE	32
#define LUA_tmpnam(b,e) { \
        strcpy(b, "/tmp/LUA_XXXXXX"); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#elif !defined(LUA_tmpnam)

#define LUA_TMPNAMBUFSIZE	L_tmpnam
#define LUA_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif


/*
** By default, LUA uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/
#if defined(LUA_USE_GMTIME_R)

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#elif !defined(l_gmtime)

#define l_gmtime(t,r)		((void)r, gmtime(t))
#define l_localtime(t,r)  	((void)r, localtime(t))

#endif



static int os_execute (LUA_State *L) {
  const char *cmd = LUAL_optstring(L, 1, NULL);
  int stat = system(cmd);
  if (cmd != NULL)
    return LUAL_execresult(L, stat);
  else {
    LUA_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}


static int os_remove (LUA_State *L) {
  const char *filename = LUAL_checkstring(L, 1);
  return LUAL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (LUA_State *L) {
  const char *fromname = LUAL_checkstring(L, 1);
  const char *toname = LUAL_checkstring(L, 2);
  return LUAL_fileresult(L, rename(fromname, toname) == 0, NULL);
}


static int os_tmpname (LUA_State *L) {
  char buff[LUA_TMPNAMBUFSIZE];
  int err;
  LUA_tmpnam(buff, err);
  if (err)
    return LUAL_error(L, "unable to generate a unique filename");
  LUA_pushstring(L, buff);
  return 1;
}


static int os_getenv (LUA_State *L) {
  LUA_pushstring(L, getenv(LUAL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}


static int os_clock (LUA_State *L) {
  LUA_pushnumber(L, ((LUA_Number)clock())/(LUA_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

static void setfield (LUA_State *L, const char *key, int value) {
  LUA_pushinteger(L, value);
  LUA_setfield(L, -2, key);
}

static void setboolfield (LUA_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  LUA_pushboolean(L, value);
  LUA_setfield(L, -2, key);
}

static int getboolfield (LUA_State *L, const char *key) {
  int res;
  LUA_getfield(L, -1, key);
  res = LUA_isnil(L, -1) ? -1 : LUA_toboolean(L, -1);
  LUA_pop(L, 1);
  return res;
}


static int getfield (LUA_State *L, const char *key, int d) {
  int res, isnum;
  LUA_getfield(L, -1, key);
  res = (int)LUA_tointegerx(L, -1, &isnum);
  if (!isnum) {
    if (d < 0)
      return LUAL_error(L, "field " LUA_QS " missing in date table", key);
    res = d;
  }
  LUA_pop(L, 1);
  return res;
}


static const char *checkoption (LUA_State *L, const char *conv, char *buff) {
  static const char *const options[] = LUA_STRFTIMEOPTIONS;
  unsigned int i;
  for (i = 0; i < sizeof(options)/sizeof(options[0]); i += 2) {
    if (*conv != '\0' && strchr(options[i], *conv) != NULL) {
      buff[1] = *conv;
      if (*options[i + 1] == '\0') {  /* one-char conversion specifier? */
        buff[2] = '\0';  /* end buffer */
        return conv + 1;
      }
      else if (*(conv + 1) != '\0' &&
               strchr(options[i + 1], *(conv + 1)) != NULL) {
        buff[2] = *(conv + 1);  /* valid two-char conversion specifier */
        buff[3] = '\0';  /* end buffer */
        return conv + 2;
      }
    }
  }
  LUAL_argerror(L, 1,
    LUA_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  /* to avoid warnings */
}


static int os_date (LUA_State *L) {
  const char *s = LUAL_optstring(L, 1, "%c");
  time_t t = LUAL_opt(L, (time_t)LUAL_checknumber, 2, time(NULL));
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip `!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL)  /* invalid date? */
    LUA_pushnil(L);
  else if (strcmp(s, "*t") == 0) {
    LUA_createtable(L, 0, 9);  /* 9 = number of fields */
    setfield(L, "SEC", stm->tm_sec);
    setfield(L, "MIN", stm->tm_min);
    setfield(L, "HOUR", stm->tm_hour);
    setfield(L, "DAY", stm->tm_mday);
    setfield(L, "MONTH", stm->tm_mon+1);
    setfield(L, "YEAR", stm->tm_year+1900);
    setfield(L, "WDAY", stm->tm_wday+1);
    setfield(L, "YDAY", stm->tm_yday+1);
    setboolfield(L, "ISDST", stm->tm_isdst);
  }
  else {
    char cc[4];
    LUAL_Buffer b;
    cc[0] = '%';
    LUAL_buffinit(L, &b);
    while (*s) {
      if (*s != '%')  /* no conversion specifier? */
        LUAL_addchar(&b, *s++);
      else {
        size_t reslen;
        char buff[200];  /* should be big enough for any conversion result */
        s = checkoption(L, s + 1, cc);
        reslen = strftime(buff, sizeof(buff), cc, stm);
        LUAL_addlstring(&b, buff, reslen);
      }
    }
    LUAL_pushresult(&b);
  }
  return 1;
}


static int os_time (LUA_State *L) {
  time_t t;
  if (LUA_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    LUAL_checktype(L, 1, LUA_TTABLE);
    LUA_settop(L, 1);  /* make sure table is at the top */
    ts.tm_sec = getfield(L, "SEC", 0);
    ts.tm_min = getfield(L, "MIN", 0);
    ts.tm_hour = getfield(L, "HOUR", 12);
    ts.tm_mday = getfield(L, "DAY", -1);
    ts.tm_mon = getfield(L, "MONTH", -1) - 1;
    ts.tm_year = getfield(L, "YEAR", -1) - 1900;
    ts.tm_isdst = getboolfield(L, "ISDST");
    t = mktime(&ts);
  }
  if (t == (time_t)(-1))
    LUA_pushnil(L);
  else
    LUA_pushnumber(L, (LUA_Number)t);
  return 1;
}


static int os_difftime (LUA_State *L) {
  LUA_pushnumber(L, difftime((time_t)(LUAL_checknumber(L, 1)),
                             (time_t)(LUAL_optnumber(L, 2, 0))));
  return 1;
}

/* }====================================================== */


static int os_setlocale (LUA_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"ALL", "COLLATE", "CTYPE", "MONETARY",
     "NUMERIC", "TIME", NULL};
  const char *l = LUAL_optstring(L, 1, NULL);
  int op = LUAL_checkoption(L, 2, "ALL", catnames);
  LUA_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (LUA_State *L) {
  int status;
  if (LUA_isboolean(L, 1))
    status = (LUA_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = LUAL_optint(L, 1, EXIT_SUCCESS);
  if (LUA_toboolean(L, 2))
    LUA_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}


static const LUAL_Reg syslib[] = {
  {"CLOCK",     os_clock},
  {"DATE",      os_date},
  {"DIFFTIME",  os_difftime},
  {"EXECUTE",   os_execute},
  {"EXIT",      os_exit},
  {"GETENV",    os_getenv},
  {"REMOVE",    os_remove},
  {"RENAME",    os_rename},
  {"SETLOCALE", os_setlocale},
  {"TIME",      os_time},
  {"TMPNAME",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



LUAMOD_API int LUAopen_os (LUA_State *L) {
  LUAL_newlib(L, syslib);
  return 1;
}

