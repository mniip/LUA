/*
** $Id: lbitlib.c,v 1.18 2013/03/19 13:19:12 roberto Exp $
** Standard library for bitwise operations
** See Copyright Notice in LUA.h
*/

#define lbitlib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


/* number of bits to consider in a number */
#if !defined(LUA_NBITS)
#define LUA_NBITS	32
#endif


#define ALLONES		(~(((~(LUA_Unsigned)0) << (LUA_NBITS - 1)) << 1))

/* macro to trim extra bits */
#define trim(x)		((x) & ALLONES)


/* builds a number with 'n' ones (1 <= n <= LUA_NBITS) */
#define mask(n)		(~((ALLONES << 1) << ((n) - 1)))


typedef LUA_Unsigned b_uint;



static b_uint andaux (LUA_State *L) {
  int i, n = LUA_gettop(L);
  b_uint r = ~(b_uint)0;
  for (i = 1; i <= n; i++)
    r &= LUAL_checkunsigned(L, i);
  return trim(r);
}


static int b_and (LUA_State *L) {
  b_uint r = andaux(L);
  LUA_pushunsigned(L, r);
  return 1;
}


static int b_test (LUA_State *L) {
  b_uint r = andaux(L);
  LUA_pushboolean(L, r != 0);
  return 1;
}


static int b_or (LUA_State *L) {
  int i, n = LUA_gettop(L);
  b_uint r = 0;
  for (i = 1; i <= n; i++)
    r |= LUAL_checkunsigned(L, i);
  LUA_pushunsigned(L, trim(r));
  return 1;
}


static int b_xor (LUA_State *L) {
  int i, n = LUA_gettop(L);
  b_uint r = 0;
  for (i = 1; i <= n; i++)
    r ^= LUAL_checkunsigned(L, i);
  LUA_pushunsigned(L, trim(r));
  return 1;
}


static int b_not (LUA_State *L) {
  b_uint r = ~LUAL_checkunsigned(L, 1);
  LUA_pushunsigned(L, trim(r));
  return 1;
}


static int b_shift (LUA_State *L, b_uint r, int i) {
  if (i < 0) {  /* shift right? */
    i = -i;
    r = trim(r);
    if (i >= LUA_NBITS) r = 0;
    else r >>= i;
  }
  else {  /* shift left */
    if (i >= LUA_NBITS) r = 0;
    else r <<= i;
    r = trim(r);
  }
  LUA_pushunsigned(L, r);
  return 1;
}


static int b_lshift (LUA_State *L) {
  return b_shift(L, LUAL_checkunsigned(L, 1), LUAL_checkint(L, 2));
}


static int b_rshift (LUA_State *L) {
  return b_shift(L, LUAL_checkunsigned(L, 1), -LUAL_checkint(L, 2));
}


static int b_arshift (LUA_State *L) {
  b_uint r = LUAL_checkunsigned(L, 1);
  int i = LUAL_checkint(L, 2);
  if (i < 0 || !(r & ((b_uint)1 << (LUA_NBITS - 1))))
    return b_shift(L, r, -i);
  else {  /* arithmetic shift for 'negative' number */
    if (i >= LUA_NBITS) r = ALLONES;
    else
      r = trim((r >> i) | ~(~(b_uint)0 >> i));  /* add signal bit */
    LUA_pushunsigned(L, r);
    return 1;
  }
}


static int b_rot (LUA_State *L, int i) {
  b_uint r = LUAL_checkunsigned(L, 1);
  i &= (LUA_NBITS - 1);  /* i = i % NBITS */
  r = trim(r);
  r = (r << i) | (r >> (LUA_NBITS - i));
  LUA_pushunsigned(L, trim(r));
  return 1;
}


static int b_lrot (LUA_State *L) {
  return b_rot(L, LUAL_checkint(L, 2));
}


static int b_rrot (LUA_State *L) {
  return b_rot(L, -LUAL_checkint(L, 2));
}


/*
** get field and width arguments for field-manipulation functions,
** checking whether they are valid.
** ('LUAL_error' called without 'return' to avoid later warnings about
** 'width' being used uninitialized.)
*/
static int fieldargs (LUA_State *L, int farg, int *width) {
  int f = LUAL_checkint(L, farg);
  int w = LUAL_optint(L, farg + 1, 1);
  LUAL_argcheck(L, 0 <= f, farg, "field cannot be negative");
  LUAL_argcheck(L, 0 < w, farg + 1, "width must be positive");
  if (f + w > LUA_NBITS)
    LUAL_error(L, "trying to access non-existent bits");
  *width = w;
  return f;
}


static int b_extract (LUA_State *L) {
  int w;
  b_uint r = LUAL_checkunsigned(L, 1);
  int f = fieldargs(L, 2, &w);
  r = (r >> f) & mask(w);
  LUA_pushunsigned(L, r);
  return 1;
}


static int b_replace (LUA_State *L) {
  int w;
  b_uint r = LUAL_checkunsigned(L, 1);
  b_uint v = LUAL_checkunsigned(L, 2);
  int f = fieldargs(L, 3, &w);
  int m = mask(w);
  v &= m;  /* erase bits outside given width */
  r = (r & ~(m << f)) | (v << f);
  LUA_pushunsigned(L, r);
  return 1;
}


static const LUAL_Reg bitlib[] = {
  {"ARSHIFT", b_arshift},
  {"BAND", b_and},
  {"BNOT", b_not},
  {"BOR", b_or},
  {"BXOR", b_xor},
  {"BTEST", b_test},
  {"EXTRACT", b_extract},
  {"LROTATE", b_lrot},
  {"LSHIFT", b_lshift},
  {"REPLACE", b_replace},
  {"RROTATE", b_rrot},
  {"RSHIFT", b_rshift},
  {NULL, NULL}
};



LUAMOD_API int LUAopen_bit32 (LUA_State *L) {
  LUAL_newlib(L, bitlib);
  return 1;
}

