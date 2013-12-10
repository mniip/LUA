/*
** $Id: Llimits.h,v 1.103 2013/02/20 14:08:56 roberto Exp $
** limits, basic types, and some other `installation-dependent' definitions
** See Copyright Notice in LUA.h
*/

#ifndef llimits_h
#define llimits_h


#include <limits.h>
#include <stddef.h>


#include "LUA.h"


typedef unsigned LUA_INT32 lu_int32;

typedef LUAI_UMEM lu_mem;

typedef LUAI_MEM l_mem;



/* chars used as small naturals (so that `char' is reserved for characters) */
typedef unsigned char lu_byte;


#define MAX_SIZET	((size_t)(~(size_t)0)-2)

#define MAX_LUMEM	((lu_mem)(~(lu_mem)0)-2)

#define MAX_LMEM	((l_mem) ((MAX_LUMEM >> 1) - 2))


#define MAX_INT (INT_MAX-2)  /* maximum value of an int (-2 for safety) */

/*
** conversion of pointer to integer
** this is for hashing only; there is no problem if the integer
** cannot hold the whole pointer value
*/
#define IntPoint(p)  ((unsigned int)(lu_mem)(p))



/* type to ensure maximum alignment */
#if !defined(LUAI_USER_ALIGNMENT_T)
#define LUAI_USER_ALIGNMENT_T	union { double u; void *s; long l; }
#endif

typedef LUAI_USER_ALIGNMENT_T L_Umaxalign;


/* result of a `usual argument conversion' over LUA_Number */
typedef LUAI_UACNUMBER l_uacNumber;


/* internal assertions for in-house debugging */
#if defined(LUA_assert)
#define check_exp(c,e)		(LUA_assert(c), (e))
/* to avoid problems with conditions too long */
#define LUA_longassert(c)	{ if (!(c)) LUA_assert(0); }
#else
#define LUA_assert(c)		((void)0)
#define check_exp(c,e)		(e)
#define LUA_longassert(c)	((void)0)
#endif

/*
** assertion for checking API calls
*/
#if !defined(LUAi_apicheck)

#if defined(LUA_USE_APICHECK)
#include <assert.h>
#define LUAi_apicheck(L,e)	assert(e)
#else
#define LUAi_apicheck(L,e)	LUA_assert(e)
#endif

#endif

#define api_check(l,e,msg)	LUAi_apicheck(l,(e) && msg)


#if !defined(UNUSED)
#define UNUSED(x)	((void)(x))	/* to avoid warnings */
#endif


#define cast(t, exp)	((t)(exp))

#define cast_byte(i)	cast(lu_byte, (i))
#define cast_num(i)	cast(LUA_Number, (i))
#define cast_int(i)	cast(int, (i))
#define cast_uchar(i)	cast(unsigned char, (i))


/*
** non-return type
*/
#if defined(__GNUC__)
#define l_noret		void __attribute__((noreturn))
#elif defined(_MSC_VER)
#define l_noret		void __declspec(noreturn)
#else
#define l_noret		void
#endif



/*
** maximum depth for nested C calls and syntactical nested non-terminals
** in a program. (Value must fit in an unsigned short int.)
*/
#if !defined(LUAI_MAXCCALLS)
#define LUAI_MAXCCALLS		200
#endif

/*
** maximum number of upvalues in a closure (both C and LUA). (Value
** must fit in an unsigned char.)
*/
#define MAXUPVAL	UCHAR_MAX


/*
** type for virtual-machine instructions
** must be an unsigned with (at Least) 4 bytes (see details in lopcodes.h)
*/
typedef lu_int32 Instruction;



/* maximum stack for a LUA function */
#define MAXSTACK	250



/* minimum size for the string table (must be power of 2) */
#if !defined(MINSTRTABSIZE)
#define MINSTRTABSIZE	32
#endif


/* minimum size for string buffer */
#if !defined(LUA_MINBUFFER)
#define LUA_MINBUFFER	32
#endif


#if !defined(LUA_lock)
#define LUA_lock(L)     ((void) 0)
#define LUA_unlock(L)   ((void) 0)
#endif

#if !defined(LUAi_threadyield)
#define LUAi_threadyield(L)     {LUA_unlock(L); LUA_lock(L);}
#endif


/*
** these macros allow user-specific actions on threads when you defined
** LUAI_EXTRASPACE and need to do something extra when a thread is
** created/deleted/resumed/yielded.
*/
#if !defined(LUAi_userstateopen)
#define LUAi_userstateopen(L)		((void)L)
#endif

#if !defined(LUAi_userstateclose)
#define LUAi_userstateclose(L)		((void)L)
#endif

#if !defined(LUAi_userstatethread)
#define LUAi_userstatethread(L,L1)	((void)L)
#endif

#if !defined(LUAi_userstatefree)
#define LUAi_userstatefree(L,L1)	((void)L)
#endif

#if !defined(LUAi_userstateresume)
#define LUAi_userstateresume(L,n)       ((void)L)
#endif

#if !defined(LUAi_userstateyield)
#define LUAi_userstateyield(L,n)        ((void)L)
#endif

/*
** LUA_number2int is a macro to convert LUA_Number to int.
** LUA_number2integer is a macro to convert LUA_Number to LUA_Integer.
** LUA_number2unsigned is a macro to convert a LUA_Number to a LUA_Unsigned.
** LUA_unsigned2number is a macro to convert a LUA_Unsigned to a LUA_Number.
** LUAi_hashnum is a macro to hash a LUA_Number value into an integer.
** The hash must be deterministic and give reasonable values for
** both small and large values (outside the range of integers).
*/

#if defined(MS_ASMTRICK) || defined(LUA_MSASMTRICK)	/* { */
/* trick with Microsoft assembler for X86 */

#define LUA_number2int(i,n)  __asm {__asm fld n   __asm fistp i}
#define LUA_number2integer(i,n)		LUA_number2int(i, n)
#define LUA_number2unsigned(i,n)  \
  {__int64 l; __asm {__asm fld n   __asm fistp l} i = (unsigned int)l;}


#elif defined(LUA_IEEE754TRICK)		/* }{ */
/* the next trick should work on any machine using IEEE754 with
   a 32-bit int type */

union LUAi_Cast { double l_d; LUA_INT32 l_p[2]; };

#if !defined(LUA_IEEEENDIAN)	/* { */
#define LUAI_EXTRAIEEE	\
  static const union LUAi_Cast ieeeendian = {-(33.0 + 6755399441055744.0)};
#define LUA_IEEEENDIANLOC	(ieeeendian.l_p[1] == 33)
#else
#define LUA_IEEEENDIANLOC	LUA_IEEEENDIAN
#define LUAI_EXTRAIEEE		/* empty */
#endif				/* } */

#define LUA_number2int32(i,n,t) \
  { LUAI_EXTRAIEEE \
    volatile union LUAi_Cast u; u.l_d = (n) + 6755399441055744.0; \
    (i) = (t)u.l_p[LUA_IEEEENDIANLOC]; }

#define LUAi_hashnum(i,n)  \
  { volatile union LUAi_Cast u; u.l_d = (n) + 1.0;  /* avoid -0 */ \
    (i) = u.l_p[0]; (i) += u.l_p[1]; }  /* add double bits for his hash */

#define LUA_number2int(i,n)		LUA_number2int32(i, n, int)
#define LUA_number2unsigned(i,n)	LUA_number2int32(i, n, LUA_Unsigned)

/* the trick can be expanded to LUA_Integer when it is a 32-bit value */
#if defined(LUA_IEEELL)
#define LUA_number2integer(i,n)		LUA_number2int32(i, n, LUA_Integer)
#endif

#endif				/* } */


/* the following definitions always work, but may be slow */

#if !defined(LUA_number2int)
#define LUA_number2int(i,n)	((i)=(int)(n))
#endif

#if !defined(LUA_number2integer)
#define LUA_number2integer(i,n)	((i)=(LUA_Integer)(n))
#endif

#if !defined(LUA_number2unsigned)	/* { */
/* the following definition assures proper modulo behavior */
#if defined(LUA_NUMBER_DOUBLE) || defined(LUA_NUMBER_FLOAT)
#include <math.h>
#define SUPUNSIGNED	((LUA_Number)(~(LUA_Unsigned)0) + 1)
#define LUA_number2unsigned(i,n)  \
	((i)=(LUA_Unsigned)((n) - floor((n)/SUPUNSIGNED)*SUPUNSIGNED))
#else
#define LUA_number2unsigned(i,n)	((i)=(LUA_Unsigned)(n))
#endif
#endif				/* } */


#if !defined(LUA_unsigned2number)
/* on several machines, coercion from unsigned to double is slow,
   so it may be worth to avoid */
#define LUA_unsigned2number(u)  \
    (((u) <= (LUA_Unsigned)INT_MAX) ? (LUA_Number)(int)(u) : (LUA_Number)(u))
#endif



#if defined(ltable_c) && !defined(LUAi_hashnum)

#include <float.h>
#include <math.h>

#define LUAi_hashnum(i,n) { int e;  \
  n = l_mathop(frexp)(n, &e) * (LUA_Number)(INT_MAX - DBL_MAX_EXP);  \
  LUA_number2int(i, n); i += e; }

#endif



/*
** macro to control inclusion of some hard tests on stack reallocation
*/
#if !defined(HARDSTACKTESTS)
#define condmovestack(L)	((void)0)
#else
/* realloc stack keeping its size */
#define condmovestack(L)	LUAD_reallocstack((L), (L)->stacksize)
#endif

#if !defined(HARDMEMTESTS)
#define condchangemem(L)	condmovestack(L)
#else
#define condchangemem(L)  \
	((void)(!(G(L)->gcrunning) || (LUAC_fullgc(L, 0), 1)))
#endif

#endif
