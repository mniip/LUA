/*
** $Id: lbaselib.c,v 1.276 2013/02/21 13:44:53 roberto Exp $
** Basic library
** See Copyright Notice in LUA.h
*/



#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lbaselib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


static int LUAB_print (LUA_State *L) {
  int n = LUA_gettop(L);  /* number of arguments */
  int i;
  LUA_getglobal(L, "TOSTRING");
  for (i=1; i<=n; i++) {
    const char *s;
    size_t l;
    LUA_pushvalue(L, -1);  /* function to be called */
    LUA_pushvalue(L, i);   /* value to print */
    LUA_call(L, 1, 1);
    s = LUA_tolstring(L, -1, &l);  /* get result */
    if (s == NULL)
      return LUAL_error(L,
         LUA_QL("TOSTRING") " must return a string to " LUA_QL("PRINT"));
    if (i>1) LUAi_writestring("\t", 1);
    LUAi_writestring(s, l);
    LUA_pop(L, 1);  /* pop result */
  }
  LUAi_writeline();
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static int LUAB_tonumber (LUA_State *L) {
  if (LUA_isnoneornil(L, 2)) {  /* standard conversion */
    int isnum;
    LUA_Number n = LUA_tonumberx(L, 1, &isnum);
    if (isnum) {
      LUA_pushnumber(L, n);
      return 1;
    }  /* else not a number; must be something */
    LUAL_checkany(L, 1);
  }
  else {
    size_t l;
    const char *s = LUAL_checklstring(L, 1, &l);
    const char *e = s + l;  /* end point for 's' */
    int base = LUAL_checkint(L, 2);
    int neg = 0;
    LUAL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    s += strspn(s, SPACECHARS);  /* skip initial spaces */
    if (*s == '-') { s++; neg = 1; }  /* handle signal */
    else if (*s == '+') s++;
    if (isalnum((unsigned char)*s)) {
      LUA_Number n = 0;
      do {
        int digit = (isdigit((unsigned char)*s)) ? *s - '0'
                       : toupper((unsigned char)*s) - 'A' + 10;
        if (digit >= base) break;  /* invalid numeral; force a fail */
        n = n * (LUA_Number)base + (LUA_Number)digit;
        s++;
      } while (isalnum((unsigned char)*s));
      s += strspn(s, SPACECHARS);  /* skip trailing spaces */
      if (s == e) {  /* no invalid trailing characters? */
        LUA_pushnumber(L, (neg) ? -n : n);
        return 1;
      }  /* else not a number */
    }  /* else not a number */
  }
  LUA_pushnil(L);  /* not a number */
  return 1;
}


static int LUAB_error (LUA_State *L) {
  int level = LUAL_optint(L, 2, 1);
  LUA_settop(L, 1);
  if (LUA_isstring(L, 1) && level > 0) {  /* add extra information? */
    LUAL_where(L, level);
    LUA_pushvalue(L, 1);
    LUA_concat(L, 2);
  }
  return LUA_error(L);
}


static int LUAB_getmetatable (LUA_State *L) {
  LUAL_checkany(L, 1);
  if (!LUA_getmetatable(L, 1)) {
    LUA_pushnil(L);
    return 1;  /* no metatable */
  }
  LUAL_getmetafield(L, 1, "__METATABLE");
  return 1;  /* returns either __metatable field (if present) or metatable */
}


static int LUAB_setmetatable (LUA_State *L) {
  int t = LUA_type(L, 2);
  LUAL_checktype(L, 1, LUA_TTABLE);
  LUAL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                    "NIL or TABLE expected");
  if (LUAL_getmetafield(L, 1, "__metatable"))
    return LUAL_error(L, "cannot change a protected metaTABLE");
  LUA_settop(L, 2);
  LUA_setmetatable(L, 1);
  return 1;
}


static int LUAB_rawequal (LUA_State *L) {
  LUAL_checkany(L, 1);
  LUAL_checkany(L, 2);
  LUA_pushboolean(L, LUA_rawequal(L, 1, 2));
  return 1;
}


static int LUAB_rawlen (LUA_State *L) {
  int t = LUA_type(L, 1);
  LUAL_argcheck(L, t == LUA_TTABLE || t == LUA_TSTRING, 1,
                   "TABLE or STRING expected");
  LUA_pushinteger(L, LUA_rawlen(L, 1));
  return 1;
}


static int LUAB_rawget (LUA_State *L) {
  LUAL_checktype(L, 1, LUA_TTABLE);
  LUAL_checkany(L, 2);
  LUA_settop(L, 2);
  LUA_rawget(L, 1);
  return 1;
}

static int LUAB_rawset (LUA_State *L) {
  LUAL_checktype(L, 1, LUA_TTABLE);
  LUAL_checkany(L, 2);
  LUAL_checkany(L, 3);
  LUA_settop(L, 3);
  LUA_rawset(L, 1);
  return 1;
}


static int LUAB_collectgarbage (LUA_State *L) {
  static const char *const opts[] = {"STOP", "RESTART", "COLLECT",
    "COUNT", "STEP", "SETPAUSE", "SETSTEPMUL",
    "SETMAJORINC", "ISRUNNING", "GENERATIONAL", "INCREMENTAL", NULL};
  static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
    LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL,
    LUA_GCSETMAJORINC, LUA_GCISRUNNING, LUA_GCGEN, LUA_GCINC};
  int o = optsnum[LUAL_checkoption(L, 1, "collect", opts)];
  int ex = LUAL_optint(L, 2, 0);
  int res = LUA_gc(L, o, ex);
  switch (o) {
    case LUA_GCCOUNT: {
      int b = LUA_gc(L, LUA_GCCOUNTB, 0);
      LUA_pushnumber(L, res + ((LUA_Number)b/1024));
      LUA_pushinteger(L, b);
      return 2;
    }
    case LUA_GCSTEP: case LUA_GCISRUNNING: {
      LUA_pushboolean(L, res);
      return 1;
    }
    default: {
      LUA_pushinteger(L, res);
      return 1;
    }
  }
}


static int LUAB_type (LUA_State *L) {
  LUAL_checkany(L, 1);
  LUA_pushstring(L, LUAL_typename(L, 1));
  return 1;
}


static int pairsmeta (LUA_State *L, const char *method, int isminustwo,
                      LUA_CFunction iter) {
  if (!LUAL_getmetafield(L, 1, method)) {  /* no metamethod? */
    LUAL_checktype(L, 1, LUA_TTABLE);  /* argument must be a table */
    LUA_pushcfunction(L, iter);  /* will return generator, */
    LUA_pushvalue(L, 1);  /* state, */
    if (isminustwo) LUA_pushinteger(L, -2);  /* and initial value */
    else LUA_pushnil(L);
  }
  else {
    LUA_pushvalue(L, 1);  /* argument 'self' to metamethod */
    LUA_call(L, 1, 3);  /* get 3 values from metamethod */
  }
  return 3;
}


static int LUAB_next (LUA_State *L) {
  LUAL_checktype(L, 1, LUA_TTABLE);
  LUA_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (LUA_next(L, 1))
    return 2;
  else {
    LUA_pushnil(L);
    return 1;
  }
}


static int LUAB_pairs (LUA_State *L) {
  return pairsmeta(L, "__PAIRS", 0, LUAB_next);
}


static int ipairsaux (LUA_State *L) {
  int i = LUAL_checkint(L, 2);
  LUAL_checktype(L, 1, LUA_TTABLE);
  i++;  /* next value */
  LUA_pushinteger(L, i);
  LUA_rawgeti(L, 1, i);
  return (LUA_isnil(L, -1)) ? 1 : 2;
}


static int LUAB_ipairs (LUA_State *L) {
  return pairsmeta(L, "__IPAIRS", 1, ipairsaux);
}


static int load_aux (LUA_State *L, int status, int envidx) {
  if (status == LUA_OK) {
    if (envidx != 0) {  /* 'env' parameter? */
      LUA_pushvalue(L, envidx);  /* environment for loaded function */
      if (!LUA_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        LUA_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    LUA_pushnil(L);
    LUA_insert(L, -2);  /* put before error message */
    return 2;  /* return nil plus error message */
  }
}


static int LUAB_loadfile (LUA_State *L) {
  const char *fname = LUAL_optstring(L, 1, NULL);
  const char *mode = LUAL_optstring(L, 2, NULL);
  int env = (!LUA_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = LUAL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read function
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic `load' function: `LUA_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (LUA_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  LUAL_checkstack(L, 2, "too many nested functions");
  LUA_pushvalue(L, 1);  /* get function */
  LUA_call(L, 0, 1);  /* call it */
  if (LUA_isnil(L, -1)) {
    LUA_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (!LUA_isstring(L, -1))
    LUAL_error(L, "reader function must return a string");
  LUA_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return LUA_tolstring(L, RESERVEDSLOT, size);
}


static int LUAB_load (LUA_State *L) {
  int status;
  size_t l;
  const char *s = LUA_tolstring(L, 1, &l);
  const char *mode = LUAL_optstring(L, 3, "bt");
  int env = (!LUA_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = LUAL_optstring(L, 2, s);
    status = LUAL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader function */
    const char *chunkname = LUAL_optstring(L, 2, "=(LOAD)");
    LUAL_checktype(L, 1, LUA_TFUNCTION);
    LUA_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = LUA_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }====================================================== */


static int dofilecont (LUA_State *L) {
  return LUA_gettop(L) - 1;
}


static int LUAB_dofile (LUA_State *L) {
  const char *fname = LUAL_optstring(L, 1, NULL);
  LUA_settop(L, 1);
  if (LUAL_loadfile(L, fname) != LUA_OK)
    return LUA_error(L);
  LUA_callk(L, 0, LUA_MULTRET, 0, dofilecont);
  return dofilecont(L);
}


static int LUAB_assert (LUA_State *L) {
  if (!LUA_toboolean(L, 1))
    return LUAL_error(L, "%s", LUAL_optstring(L, 2, "assertion failed!"));
  return LUA_gettop(L);
}


static int LUAB_select (LUA_State *L) {
  int n = LUA_gettop(L);
  if (LUA_type(L, 1) == LUA_TSTRING && *LUA_tostring(L, 1) == '#') {
    LUA_pushinteger(L, n-1);
    return 1;
  }
  else {
    int i = LUAL_checkint(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    LUAL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - i;
  }
}


static int finishpcall (LUA_State *L, int status) {
  if (!LUA_checkstack(L, 1)) {  /* no space for extra boolean? */
    LUA_settop(L, 0);  /* create space for return values */
    LUA_pushboolean(L, 0);
    LUA_pushstring(L, "stack overflow");
    return 2;  /* return false, msg */
  }
  LUA_pushboolean(L, status);  /* first result (status) */
  LUA_replace(L, 1);  /* put first result in first slot */
  return LUA_gettop(L);
}


static int pcallcont (LUA_State *L) {
  int status = LUA_getctx(L, NULL);
  return finishpcall(L, (status == LUA_YIELD));
}


static int LUAB_pcall (LUA_State *L) {
  int status;
  LUAL_checkany(L, 1);
  LUA_pushnil(L);
  LUA_insert(L, 1);  /* create space for status result */
  status = LUA_pcallk(L, LUA_gettop(L) - 2, LUA_MULTRET, 0, 0, pcallcont);
  return finishpcall(L, (status == LUA_OK));
}


static int LUAB_xpcall (LUA_State *L) {
  int status;
  int n = LUA_gettop(L);
  LUAL_argcheck(L, n >= 2, 2, "value expected");
  LUA_pushvalue(L, 1);  /* exchange function... */
  LUA_copy(L, 2, 1);  /* ...and error handler */
  LUA_replace(L, 2);
  status = LUA_pcallk(L, n - 2, LUA_MULTRET, 1, 0, pcallcont);
  return finishpcall(L, (status == LUA_OK));
}


static int LUAB_tostring (LUA_State *L) {
  LUAL_checkany(L, 1);
  LUAL_tolstring(L, 1, NULL);
  return 1;
}


/*
** {======================================================
** Function metatable
** =======================================================
*/


static int aux_func_sub_closure(LUA_State *L) {
  int narg = LUA_gettop(L);
  int i=-1;
  LUA_pushvalue(L, LUA_upvalueindex(2));
  LUA_pushvalue(L, LUA_upvalueindex(1));
  while (1) {
    int v;
    LUA_pushnumber(L, i);
    LUA_gettable(L, narg+1);
    if(LUA_isnumber(L, -1)) {
      v = LUA_tonumber(L, -1);
      LUA_pop(L, 1);
      if(0<v && v<=narg)
        LUA_pushvalue(L, v);
      else
        LUA_pushnil(L);
    }
    else {
      LUA_pop(L, 1);
      break;
    }
    i++;
  }
  LUA_call(L, i+1, LUA_MULTRET);
  return LUA_gettop(L)-narg-1;
}


static int aux_func_sub(LUA_State *L) {
  LUAL_checktype(L, 1, LUA_TFUNCTION);
  LUAL_checktype(L, 2, LUA_TTABLE);
  LUA_pushcclosure(L, aux_func_sub_closure, 2);
  return 1;
}


static int aux_func_mod_closure(LUA_State *L) {
  int narg = LUA_gettop(L);
  int i;
  LUA_pushvalue(L, LUA_upvalueindex(1));
  for (i=1; i<=narg; i++)
    LUA_pushvalue(L, i);
  LUA_call(L, narg, LUA_MULTRET);
  int nres = LUA_gettop(L)-narg;
  LUA_pushvalue(L, LUA_upvalueindex(2));
  for (i=1; i<=nres; i++)
    LUA_pushvalue(L, i+narg);
  LUA_call(L, nres, LUA_MULTRET);
  return LUA_gettop(L)-narg-nres;
}


static int aux_func_mod(LUA_State *L) {
  LUAL_checktype(L, 1, LUA_TFUNCTION);
  LUAL_checktype(L, 2, LUA_TFUNCTION);
  LUA_pushcclosure(L, aux_func_mod_closure, 2);
  return 1;
}


static int aux_func_index_closure(LUA_State *L) {
  int narg = LUA_gettop(L);
  int i;
  LUA_pushvalue(L, LUA_upvalueindex(1));
  LUA_pushvalue(L, LUA_upvalueindex(2));
  for (i=1; i<=narg; i++)
    LUA_pushvalue(L, i);
  LUA_call(L, narg+1, LUA_MULTRET);
  return LUA_gettop(L)-narg;
}


static int aux_func_index(LUA_State *L) {
  LUAL_checktype(L, 1, LUA_TFUNCTION);
  LUA_pushcclosure(L, aux_func_index_closure, 2);
  return 1;
}


static void aux_funcmeta(LUA_State *L) {
  LUA_pushcfunction(L, aux_func_sub);
  LUA_newtable(L); /* the metatable */

  LUA_pushcfunction(L, aux_func_sub);
  LUA_setfield(L, -2, "__SUB");

  LUA_pushcfunction(L, aux_func_mod);
  LUA_setfield(L, -2, "__MOD");

  LUA_pushcfunction(L, aux_func_index);
  LUA_setfield(L, -2, "__INDEX");

  LUA_setmetatable(L, -2);
  LUA_pop(L, 1);
}

#define FOP_CLOSURE(NAME, CODE)static int aux_fop_##NAME(LUA_State *L) {CODE return 1;}
FOP_CLOSURE(add, LUA_settop(L, 2); LUA_arith(L, LUA_OPADD);)
FOP_CLOSURE(sub, LUA_settop(L, 2); LUA_arith(L, LUA_OPSUB);)
FOP_CLOSURE(mul, LUA_settop(L, 2); LUA_arith(L, LUA_OPMUL);)
FOP_CLOSURE(div, LUA_settop(L, 2); LUA_arith(L, LUA_OPDIV);)
FOP_CLOSURE(mod, LUA_settop(L, 2); LUA_arith(L, LUA_OPMOD);)
FOP_CLOSURE(pow, LUA_settop(L, 2); LUA_arith(L, LUA_OPPOW);)
FOP_CLOSURE(unm, LUA_settop(L, 1); LUA_arith(L, LUA_OPUNM);)
FOP_CLOSURE(eq, LUA_settop(L, 2); LUA_pushboolean(L, LUA_compare(L, 1, 2, LUA_OPEQ));)
FOP_CLOSURE(lt, LUA_settop(L, 2); LUA_pushboolean(L, LUA_compare(L, 1, 2, LUA_OPLT));)
FOP_CLOSURE(gt, LUA_settop(L, 2); LUA_pushboolean(L, !LUA_compare(L, 1, 2, LUA_OPLE));)
FOP_CLOSURE(or, LUA_settop(L, 2); if(LUA_toboolean(L, 1))LUA_pop(L, 1);)
FOP_CLOSURE(and, LUA_settop(L, 2); if(!LUA_toboolean(L, 1))LUA_pop(L, 1);)
FOP_CLOSURE(not, LUA_settop(L, 1); LUA_pushboolean(L, !LUA_toboolean(L, 1));)
FOP_CLOSURE(idx, LUA_settop(L, 2); LUA_gettable(L, 1);)
FOP_CLOSURE(self, LUA_settop(L, 2); LUA_gettable(L, 1); LUA_pushvalue(L, 1); LUA_gettable(L, -2);)
FOP_CLOSURE(nop, return 0;)

static int LUAB_fop(LUA_State *L) {
  if(LUA_isnoneornil(L, 1)) {
      LUA_pushcfunction(L, aux_fop_nop);
  }
  else {
    switch(LUAL_optstring(L, 1, "")[0]) {
      case '+': LUA_pushcfunction(L, aux_fop_add); break;
      case '-': LUA_pushcfunction(L, aux_fop_sub); break;
      case '*': LUA_pushcfunction(L, aux_fop_mul); break;
      case '/': LUA_pushcfunction(L, aux_fop_div); break;
      case '%': LUA_pushcfunction(L, aux_fop_mod); break;
      case '^': LUA_pushcfunction(L, aux_fop_pow); break;
      case '_': LUA_pushcfunction(L, aux_fop_unm); break;
      case '=': LUA_pushcfunction(L, aux_fop_eq); break;
      case '<': LUA_pushcfunction(L, aux_fop_lt); break;
      case '>': LUA_pushcfunction(L, aux_fop_gt); break;
      case '|': LUA_pushcfunction(L, aux_fop_or); break;
      case '&': LUA_pushcfunction(L, aux_fop_and); break;
      case '!': LUA_pushcfunction(L, aux_fop_not); break;
      case '.': LUA_pushcfunction(L, aux_fop_idx); break;
      case ':': LUA_pushcfunction(L, aux_fop_self); break;
      default: LUA_pushcfunction(L, aux_fop_nop); break;
    }
  }
  return 1;
}

/* }====================================================== */


static const LUAL_Reg base_funcs[] = {
  {"ASSERT", LUAB_assert},
  {"COLLECTGARBAGE", LUAB_collectgarbage},
  {"DOFILE", LUAB_dofile},
  {"ERROR", LUAB_error},
  {"GETMETATABLE", LUAB_getmetatable},
  {"IPAIRS", LUAB_ipairs},
  {"LOADFILE", LUAB_loadfile},
  {"LOAD", LUAB_load},
#if defined(LUA_COMPAT_LOADSTRING)
  {"LOADSTRING", LUAB_load},
#endif
  {"NEXT", LUAB_next},
  {"PAIRS", LUAB_pairs},
  {"PCALL", LUAB_pcall},
  {"PRINT", LUAB_print},
  {"RAWEQUAL", LUAB_rawequal},
  {"RAWLEN", LUAB_rawlen},
  {"RAWGET", LUAB_rawget},
  {"RAWSET", LUAB_rawset},
  {"SELECT", LUAB_select},
  {"SETMETATABLE", LUAB_setmetatable},
  {"TONUMBER", LUAB_tonumber},
  {"TOSTRING", LUAB_tostring},
  {"TYPE", LUAB_type},
  {"XPCALL", LUAB_xpcall},
  {"FOP", LUAB_fop},
  {NULL, NULL}
};


LUAMOD_API int LUAopen_base (LUA_State *L) {
  /* set global _G */
  LUA_pushglobaltable(L);
  LUA_pushglobaltable(L);
  LUA_setfield(L, -2, "_G");
  /* open lib into global table */
  LUAL_setfuncs(L, base_funcs, 0);
  LUA_pushliteral(L, LUA_VERSION);
  LUA_setfield(L, -2, "_VERSION");  /* set global _VERSION */
  aux_funcmeta(L);
  return 1;
}

