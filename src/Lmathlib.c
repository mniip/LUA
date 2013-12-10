/*
** $Id: lmathlib.c,v 1.83 2013/03/07 18:21:32 roberto Exp $
** Standard mathematical library
** See Copyright Notice in LUA.h
*/


#include <stdlib.h>
#include <math.h>

#define lmathlib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


#undef PI
#define PI	((LUA_Number)(3.1415926535897932384626433832795))
#define RADIANS_PER_DEGREE	((LUA_Number)(PI/180.0))



static int math_abs (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(fabs)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_sin (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(sin)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_sinh (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(sinh)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_cos (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(cos)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_cosh (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(cosh)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_tan (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(tan)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_tanh (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(tanh)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_asin (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(asin)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_acos (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(acos)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_atan (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(atan)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_atan2 (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(atan2)(LUAL_checknumber(L, 1),
                                LUAL_checknumber(L, 2)));
  return 1;
}

static int math_ceil (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(ceil)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_floor (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(floor)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_fmod (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(fmod)(LUAL_checknumber(L, 1),
                               LUAL_checknumber(L, 2)));
  return 1;
}

static int math_modf (LUA_State *L) {
  LUA_Number ip;
  LUA_Number fp = l_mathop(modf)(LUAL_checknumber(L, 1), &ip);
  LUA_pushnumber(L, ip);
  LUA_pushnumber(L, fp);
  return 2;
}

static int math_sqrt (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(sqrt)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_pow (LUA_State *L) {
  LUA_Number x = LUAL_checknumber(L, 1);
  LUA_Number y = LUAL_checknumber(L, 2);
  LUA_pushnumber(L, l_mathop(pow)(x, y));
  return 1;
}

static int math_log (LUA_State *L) {
  LUA_Number x = LUAL_checknumber(L, 1);
  LUA_Number res;
  if (LUA_isnoneornil(L, 2))
    res = l_mathop(log)(x);
  else {
    LUA_Number base = LUAL_checknumber(L, 2);
    if (base == (LUA_Number)10.0) res = l_mathop(log10)(x);
    else res = l_mathop(log)(x)/l_mathop(log)(base);
  }
  LUA_pushnumber(L, res);
  return 1;
}

#if defined(LUA_COMPAT_LOG10)
static int math_log10 (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(log10)(LUAL_checknumber(L, 1)));
  return 1;
}
#endif

static int math_exp (LUA_State *L) {
  LUA_pushnumber(L, l_mathop(exp)(LUAL_checknumber(L, 1)));
  return 1;
}

static int math_deg (LUA_State *L) {
  LUA_pushnumber(L, LUAL_checknumber(L, 1)/RADIANS_PER_DEGREE);
  return 1;
}

static int math_rad (LUA_State *L) {
  LUA_pushnumber(L, LUAL_checknumber(L, 1)*RADIANS_PER_DEGREE);
  return 1;
}

static int math_frexp (LUA_State *L) {
  int e;
  LUA_pushnumber(L, l_mathop(frexp)(LUAL_checknumber(L, 1), &e));
  LUA_pushinteger(L, e);
  return 2;
}

static int math_ldexp (LUA_State *L) {
  LUA_Number x = LUAL_checknumber(L, 1);
  int ep = LUAL_checkint(L, 2);
  LUA_pushnumber(L, l_mathop(ldexp)(x, ep));
  return 1;
}



static int math_min (LUA_State *L) {
  int n = LUA_gettop(L);  /* number of arguments */
  LUA_Number dmin = LUAL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    LUA_Number d = LUAL_checknumber(L, i);
    if (d < dmin)
      dmin = d;
  }
  LUA_pushnumber(L, dmin);
  return 1;
}


static int math_max (LUA_State *L) {
  int n = LUA_gettop(L);  /* number of arguments */
  LUA_Number dmax = LUAL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    LUA_Number d = LUAL_checknumber(L, i);
    if (d > dmax)
      dmax = d;
  }
  LUA_pushnumber(L, dmax);
  return 1;
}


static int math_random (LUA_State *L) {
  /* the `%' avoids the (rare) case of r==1, and is needed also because on
     some systems (SunOS!) `rand()' may return a value larger than RAND_MAX */
  LUA_Number r = (LUA_Number)(rand()%RAND_MAX) / (LUA_Number)RAND_MAX;
  switch (LUA_gettop(L)) {  /* check number of arguments */
    case 0: {  /* no arguments */
      LUA_pushnumber(L, r);  /* Number between 0 and 1 */
      break;
    }
    case 1: {  /* only upper limit */
      LUA_Number u = LUAL_checknumber(L, 1);
      LUAL_argcheck(L, (LUA_Number)1.0 <= u, 1, "interval is empty");
      LUA_pushnumber(L, l_mathop(floor)(r*u) + (LUA_Number)(1.0));  /* [1, u] */
      break;
    }
    case 2: {  /* lower and upper limits */
      LUA_Number l = LUAL_checknumber(L, 1);
      LUA_Number u = LUAL_checknumber(L, 2);
      LUAL_argcheck(L, l <= u, 2, "interval is empty");
      LUA_pushnumber(L, l_mathop(floor)(r*(u-l+1)) + l);  /* [l, u] */
      break;
    }
    default: return LUAL_error(L, "wrong number of arguments");
  }
  return 1;
}


static int math_randomseed (LUA_State *L) {
  srand(LUAL_checkunsigned(L, 1));
  (void)rand(); /* discard first value to avoid undesirable correlations */
  return 0;
}


static const LUAL_Reg mathlib[] = {
  {"ABS",   math_abs},
  {"ACOS",  math_acos},
  {"ASIN",  math_asin},
  {"ATAN2", math_atan2},
  {"ATAN",  math_atan},
  {"CEIL",  math_ceil},
  {"COSH",   math_cosh},
  {"COS",   math_cos},
  {"DEG",   math_deg},
  {"EXP",   math_exp},
  {"FLOOR", math_floor},
  {"FMOD",   math_fmod},
  {"FREXP", math_frexp},
  {"LDEXP", math_ldexp},
#if defined(LUA_COMPAT_LOG10)
  {"LOG10", math_log10},
#endif
  {"LOG",   math_log},
  {"MAX",   math_max},
  {"MIN",   math_min},
  {"MODF",   math_modf},
  {"POW",   math_pow},
  {"RAD",   math_rad},
  {"RANDOM",     math_random},
  {"RANDOMSEED", math_randomseed},
  {"SINH",   math_sinh},
  {"SIN",   math_sin},
  {"SQRT",  math_sqrt},
  {"TANH",   math_tanh},
  {"TAN",   math_tan},
  {NULL, NULL}
};


/*
** Open math library
*/
LUAMOD_API int LUAopen_math (LUA_State *L) {
  LUAL_newlib(L, mathlib);
  LUA_pushnumber(L, PI);
  LUA_setfield(L, -2, "PI");
  LUA_pushnumber(L, HUGE_VAL);
  LUA_setfield(L, -2, "HUGE");
  return 1;
}

