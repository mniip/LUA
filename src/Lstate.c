/*
** $Id: lstate.c,v 2.99 2012/10/02 17:40:53 roberto Exp $
** Global State
** See Copyright Notice in LUA.h
*/


#include <stddef.h>
#include <string.h>

#define lstate_c
#define LUA_CORE

#include "LUA.h"

#include "Lapi.h"
#include "Ldebug.h"
#include "Ldo.h"
#include "Lfunc.h"
#include "Lgc.h"
#include "Llex.h"
#include "Lmem.h"
#include "Lstate.h"
#include "Lstring.h"
#include "Ltable.h"
#include "Ltm.h"


#if !defined(LUAI_GCPAUSE)
#define LUAI_GCPAUSE	200  /* 200% */
#endif

#if !defined(LUAI_GCMAJOR)
#define LUAI_GCMAJOR	200  /* 200% */
#endif

#if !defined(LUAI_GCMUL)
#define LUAI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */
#endif


#define MEMERRMSG	"not enough memory"


/*
** a macro to help the creation of a unique random seed when a state is
** created; the seed is used to randomize hashes.
*/
#if !defined(LUAi_makeseed)
#include <time.h>
#define LUAi_makeseed()		cast(unsigned int, time(NULL))
#endif



/*
** thread state + extra space
*/
typedef struct LX {
#if defined(LUAI_EXTRASPACE)
  char buff[LUAI_EXTRASPACE];
#endif
  LUA_State l;
} LX;


/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
  LX l;
  global_State g;
} LG;



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** Compute an initial seed as random as possible. In ANSI, rely on
** Address Space Layout Randomization (if present) to increase
** randomness..
*/
#define addbuff(b,p,e) \
  { size_t t = cast(size_t, e); \
    memcpy(buff + p, &t, sizeof(t)); p += sizeof(t); }

static unsigned int makeseed (LUA_State *L) {
  char buff[4 * sizeof(size_t)];
  unsigned int h = LUAi_makeseed();
  int p = 0;
  addbuff(buff, p, L);  /* heap variable */
  addbuff(buff, p, &h);  /* local variable */
  addbuff(buff, p, LUAO_nilobject);  /* global variable */
  addbuff(buff, p, &LUA_newstate);  /* public function */
  LUA_assert(p == sizeof(buff));
  return LUAS_hash(buff, p, h);
}


/*
** set GCdebt to a new value keeping the value (totalbytes + GCdebt)
** invariant
*/
void LUAE_setdebt (global_State *g, l_mem debt) {
  g->totalbytes -= (debt - g->GCdebt);
  g->GCdebt = debt;
}


CallInfo *LUAE_extendCI (LUA_State *L) {
  CallInfo *ci = LUAM_new(L, CallInfo);
  LUA_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  return ci;
}


void LUAE_freeCI (LUA_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    LUAM_free(L, ci);
  }
}


static void stack_init (LUA_State *L1, LUA_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack = LUAM_newvector(L, BASIC_STACK_SIZE, TValue);
  L1->stacksize = BASIC_STACK_SIZE;
  for (i = 0; i < BASIC_STACK_SIZE; i++)
    setnilvalue(L1->stack + i);  /* erase new stack */
  L1->top = L1->stack;
  L1->stack_last = L1->stack + L1->stacksize - EXTRA_STACK;
  /* initialize first ci */
  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = L1->top;
  setnilvalue(L1->top++);  /* 'function' entry for this 'ci' */
  ci->top = L1->top + LUA_MINSTACK;
  L1->ci = ci;
}


static void freestack (LUA_State *L) {
  if (L->stack == NULL)
    return;  /* stack not completely built yet */
  L->ci = &L->base_ci;  /* free the entire 'ci' list */
  LUAE_freeCI(L);
  LUAM_freearray(L, L->stack, L->stacksize);  /* free stack array */
}


/*
** Create registry table and its predefined values
*/
static void init_registry (LUA_State *L, global_State *g) {
  TValue mt;
  /* create registry */
  Table *registry = LUAH_new(L);
  sethvalue(L, &g->l_registry, registry);
  LUAH_resize(L, registry, LUA_RIDX_LAST, 0);
  /* registry[LUA_RIDX_MAINTHREAD] = L */
  setthvalue(L, &mt, L);
  LUAH_setint(L, registry, LUA_RIDX_MAINTHREAD, &mt);
  /* registry[LUA_RIDX_GLOBALS] = table of globals */
  sethvalue(L, &mt, LUAH_new(L));
  LUAH_setint(L, registry, LUA_RIDX_GLOBALS, &mt);
}


/*
** open parts of the state that may cause memory-allocation errors
*/
static void f_LUAopen (LUA_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  LUAS_resize(L, MINSTRTABSIZE);  /* initial size of string table */
  LUAT_init(L);
  LUAX_init(L);
  /* pre-create memory-error message */
  g->memerrmsg = LUAS_newliteral(L, MEMERRMSG);
  LUAS_fix(g->memerrmsg);  /* it should never be collected */
  g->gcrunning = 1;  /* allow gc */
}


/*
** preinitialize a state with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_state (LUA_State *L, global_State *g) {
  G(L) = g;
  L->stack = NULL;
  L->ci = NULL;
  L->stacksize = 0;
  L->errorJmp = NULL;
  L->nCcalls = 0;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->nny = 1;
  L->status = LUA_OK;
  L->errfunc = 0;
}


static void close_state (LUA_State *L) {
  global_State *g = G(L);
  LUAF_close(L, L->stack);  /* close all upvalues for this thread */
  LUAC_freeallobjects(L);  /* collect all objects */
  LUAM_freearray(L, G(L)->strt.hash, G(L)->strt.size);
  LUAZ_freebuffer(L, &g->buff);
  freestack(L);
  LUA_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);  /* free main block */
}


LUA_API LUA_State *LUA_newthread (LUA_State *L) {
  LUA_State *L1;
  LUA_lock(L);
  LUAC_checkGC(L);
  L1 = &LUAC_newobj(L, LUA_TTHREAD, sizeof(LX), NULL, offsetof(LX, l))->th;
  setthvalue(L, L->top, L1);
  api_incr_top(L);
  preinit_state(L1, G(L));
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  LUAi_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  LUA_unlock(L);
  return L1;
}


void LUAE_freethread (LUA_State *L, LUA_State *L1) {
  LX *l = fromstate(L1);
  LUAF_close(L1, L1->stack);  /* close all upvalues for this thread */
  LUA_assert(L1->openupval == NULL);
  LUAi_userstatefree(L, L1);
  freestack(L1);
  LUAM_free(L, l);
}


LUA_API LUA_State *LUA_newstate (LUA_Alloc f, void *ud) {
  int i;
  LUA_State *L;
  global_State *g;
  LG *l = cast(LG *, (*f)(ud, NULL, LUA_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;
  L = &l->l.l;
  g = &l->g;
  L->next = NULL;
  L->tt = LUA_TTHREAD;
  g->currentwhite = bit2mask(WHITE0BIT, FIXEDBIT);
  L->marked = LUAC_white(g);
  g->gckind = KGC_NORMAL;
  preinit_state(L, g);
  g->frealloc = f;
  g->ud = ud;
  g->mainthread = L;
  g->seed = makeseed(L);
  g->uvhead.u.l.prev = &g->uvhead;
  g->uvhead.u.l.next = &g->uvhead;
  g->gcrunning = 0;  /* no GC while building state */
  g->GCestimate = 0;
  g->strt.size = 0;
  g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  LUAZ_initbuffer(L, &g->buff);
  g->panic = NULL;
  g->version = LUA_version(NULL);
  g->gcstate = GCSpause;
  g->allgc = NULL;
  g->finobj = NULL;
  g->tobefnz = NULL;
  g->sweepgc = g->sweepfin = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->totalbytes = sizeof(LG);
  g->GCdebt = 0;
  g->gcpause = LUAI_GCPAUSE;
  g->gcmajorinc = LUAI_GCMAJOR;
  g->gcstepmul = LUAI_GCMUL;
  for (i=0; i < LUA_NUMTAGS; i++) g->mt[i] = NULL;
  if (LUAD_rawrunprotected(L, f_LUAopen, NULL) != LUA_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  else
    LUAi_userstateopen(L);
  return L;
}


LUA_API void LUA_close (LUA_State *L) {
  L = G(L)->mainthread;  /* only the main thread can be closed */
  LUA_lock(L);
  LUAi_userstateclose(L);
  close_state(L);
}


