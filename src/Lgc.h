/*
** $Id: Lgc.h,v 2.58 2012/09/11 12:53:08 roberto Exp $
** Garbage Collector
** See Copyright Notice in LUA.h
*/

#ifndef lgc_h
#define lgc_h


#include "Lobject.h"
#include "Lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/



/* how much to allocate before next GC step */
#if !defined(GCSTEPSIZE)
/* ~100 small strings */
#define GCSTEPSIZE	(cast_int(100 * sizeof(TString)))
#endif


/*
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0
#define GCSatomic	1
#define GCSsweepstring	2
#define GCSsweepudata	3
#define GCSsweep	4
#define GCSpause	5


#define issweepphase(g)  \
	(GCSsweepstring <= (g)->gcstate && (g)->gcstate <= GCSsweep)

#define isgenerational(g)	((g)->gckind == KGC_GEN)

/*
** macros to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a non-generational collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again. During a generational collection, the
** invariant must be kept all times.
*/

#define keepinvariant(g)	(isgenerational(g) || g->gcstate <= GCSatomic)


/*
** Outside the collector, the state in generational mode is kept in
** 'propagate', so 'keepinvariant' is always true.
*/
#define keepinvariantout(g)  \
  check_exp(g->gcstate == GCSpropagate || !isgenerational(g),  \
            g->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))


/* Layout for bit use in `marked' field: */
#define WHITE0BIT	0  /* object is white (type 0) */
#define WHITE1BIT	1  /* object is white (type 1) */
#define BLACKBIT	2  /* object is black */
#define FINALIZEDBIT	3  /* object has been separated for finalization */
#define SEPARATED	4  /* object is in 'finobj' list or in 'tobefnz' */
#define FIXEDBIT	5  /* object is fixed (should not be collected) */
#define OLDBIT		6  /* object is old (only in generational mode) */
/* bit 7 is currently used by tests (LUAL_checkmemory) */

#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)


#define iswhite(x)      testbits((x)->gch.marked, WHITEBITS)
#define isblack(x)      testbit((x)->gch.marked, BLACKBIT)
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->gch.marked, WHITEBITS | bitmask(BLACKBIT)))

#define isold(x)	testbit((x)->gch.marked, OLDBIT)

/* MOVE OLD rule: whenever an object is moved to the beginning of
   a GC list, its old bit must be cleared */
#define resetoldbit(o)	resetbit((o)->gch.marked, OLDBIT)

#define otherwhite(g)	(g->currentwhite ^ WHITEBITS)
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->gch.marked)

#define changewhite(x)	((x)->gch.marked ^= WHITEBITS)
#define gray2black(x)	l_setbit((x)->gch.marked, BLACKBIT)

#define valiswhite(x)	(iscollectable(x) && iswhite(gcvalue(x)))

#define LUAC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)


#define LUAC_condGC(L,c) \
	{if (G(L)->GCdebt > 0) {c;}; condchangemem(L);}
#define LUAC_checkGC(L)		LUAC_condGC(L, LUAC_step(L);)


#define LUAC_barrier(L,p,v) { if (valiswhite(v) && isblack(obj2gco(p)))  \
	LUAC_barrier_(L,obj2gco(p),gcvalue(v)); }

#define LUAC_barrierback(L,p,v) { if (valiswhite(v) && isblack(obj2gco(p)))  \
	LUAC_barrierback_(L,p); }

#define LUAC_objbarrier(L,p,o)  \
	{ if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) \
		LUAC_barrier_(L,obj2gco(p),obj2gco(o)); }

#define LUAC_objbarrierback(L,p,o)  \
   { if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) LUAC_barrierback_(L,p); }

#define LUAC_barrierproto(L,p,c) \
   { if (isblack(obj2gco(p))) LUAC_barrierproto_(L,p,c); }

LUAI_FUNC void LUAC_freeallobjects (LUA_State *L);
LUAI_FUNC void LUAC_step (LUA_State *L);
LUAI_FUNC void LUAC_forcestep (LUA_State *L);
LUAI_FUNC void LUAC_runtilstate (LUA_State *L, int statesmask);
LUAI_FUNC void LUAC_fullgc (LUA_State *L, int isemergency);
LUAI_FUNC GCObject *LUAC_newobj (LUA_State *L, int tt, size_t sz,
                                 GCObject **list, int offset);
LUAI_FUNC void LUAC_barrier_ (LUA_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void LUAC_barrierback_ (LUA_State *L, GCObject *o);
LUAI_FUNC void LUAC_barrierproto_ (LUA_State *L, Proto *p, Closure *c);
LUAI_FUNC void LUAC_checkfinalizer (LUA_State *L, GCObject *o, Table *mt);
LUAI_FUNC void LUAC_checkupvalcolor (global_State *g, UpVal *uv);
LUAI_FUNC void LUAC_changemode (LUA_State *L, int mode);

#endif
