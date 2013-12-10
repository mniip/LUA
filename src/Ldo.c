/*
** $Id: ldo.c,v 2.108 2012/10/01 14:05:04 roberto Exp $
** Stack and Call structure of LUA
** See Copyright Notice in LUA.h
*/


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define ldo_c
#define LUA_CORE

#include "LUA.h"

#include "Lapi.h"
#include "Ldebug.h"
#include "Ldo.h"
#include "Lfunc.h"
#include "Lgc.h"
#include "Lmem.h"
#include "Lobject.h"
#include "Lopcodes.h"
#include "Lparser.h"
#include "Lstate.h"
#include "Lstring.h"
#include "Ltable.h"
#include "Ltm.h"
#include "Lundump.h"
#include "Lvm.h"
#include "Lzio.h"




/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** LUAI_THROW/LUAI_TRY define how LUA does exception handling. By
** default, LUA handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
#if !defined(LUAI_THROW)

#if defined(__cplusplus) && !defined(LUA_USE_LONGJMP)
/* C++ exceptions */
#define LUAI_THROW(L,c)		throw(c)
#define LUAI_TRY(L,c,a) \
	try { a } catch(...) { if ((c)->status == 0) (c)->status = -1; }
#define LUAi_jmpbuf		int  /* dummy variable */

#elif defined(LUA_USE_ULONGJMP)
/* in Unix, try _longjmp/_setjmp (more efficient) */
#define LUAI_THROW(L,c)		_longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)		if (_setjmp((c)->b) == 0) { a }
#define LUAi_jmpbuf		jmp_buf

#else
/* default handling with long jumps */
#define LUAI_THROW(L,c)		longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)		if (setjmp((c)->b) == 0) { a }
#define LUAi_jmpbuf		jmp_buf

#endif

#endif



/* chain list of long jump buffers */
struct LUA_longjmp {
  struct LUA_longjmp *previous;
  LUAi_jmpbuf b;
  volatile int status;  /* error code */
};


static void seterrorobj (LUA_State *L, int errcode, StkId oldtop) {
  switch (errcode) {
    case LUA_ERRMEM: {  /* memory error? */
      setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
      break;
    }
    case LUA_ERRERR: {
      setsvalue2s(L, oldtop, LUAS_newliteral(L, "error in error handling"));
      break;
    }
    default: {
      setobjs2s(L, oldtop, L->top - 1);  /* error message on current top */
      break;
    }
  }
  L->top = oldtop + 1;
}


l_noret LUAD_throw (LUA_State *L, int errcode) {
  if (L->errorJmp) {  /* thread has an error handler? */
    L->errorJmp->status = errcode;  /* set status */
    LUAI_THROW(L, L->errorJmp);  /* jump to it */
  }
  else {  /* thread has no error handler */
    L->status = cast_byte(errcode);  /* mark it as dead */
    if (G(L)->mainthread->errorJmp) {  /* main thread has a handler? */
      setobjs2s(L, G(L)->mainthread->top++, L->top - 1);  /* copy error obj. */
      LUAD_throw(G(L)->mainthread, errcode);  /* re-throw in main thread */
    }
    else {  /* no handler at all; abort */
      if (G(L)->panic) {  /* panic function? */
        LUA_unlock(L);
        G(L)->panic(L);  /* call it (last chance to jump out) */
      }
      abort();
    }
  }
}


int LUAD_rawrunprotected (LUA_State *L, Pfunc f, void *ud) {
  unsigned short oldnCcalls = L->nCcalls;
  struct LUA_longjmp lj;
  lj.status = LUA_OK;
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  LUAI_TRY(L, &lj,
    (*f)(L, ud);
  );
  L->errorJmp = lj.previous;  /* restore old error handler */
  L->nCcalls = oldnCcalls;
  return lj.status;
}

/* }====================================================== */


static void correctstack (LUA_State *L, TValue *oldstack) {
  CallInfo *ci;
  GCObject *up;
  L->top = (L->top - oldstack) + L->stack;
  for (up = L->openupval; up != NULL; up = up->gch.next)
    gco2uv(up)->v = (gco2uv(up)->v - oldstack) + L->stack;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top = (ci->top - oldstack) + L->stack;
    ci->func = (ci->func - oldstack) + L->stack;
    if (isLUA(ci))
      ci->u.l.base = (ci->u.l.base - oldstack) + L->stack;
  }
}


/* some space for error handling */
#define ERRORSTACKSIZE	(LUAI_MAXSTACK + 200)


void LUAD_reallocstack (LUA_State *L, int newsize) {
  TValue *oldstack = L->stack;
  int lim = L->stacksize;
  LUA_assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);
  LUA_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK);
  LUAM_reallocvector(L, L->stack, L->stacksize, newsize, TValue);
  for (; lim < newsize; lim++)
    setnilvalue(L->stack + lim); /* erase new segment */
  L->stacksize = newsize;
  L->stack_last = L->stack + newsize - EXTRA_STACK;
  correctstack(L, oldstack);
}


void LUAD_growstack (LUA_State *L, int n) {
  int size = L->stacksize;
  if (size > LUAI_MAXSTACK)  /* error after extra size? */
    LUAD_throw(L, LUA_ERRERR);
  else {
    int needed = cast_int(L->top - L->stack) + n + EXTRA_STACK;
    int newsize = 2 * size;
    if (newsize > LUAI_MAXSTACK) newsize = LUAI_MAXSTACK;
    if (newsize < needed) newsize = needed;
    if (newsize > LUAI_MAXSTACK) {  /* stack overflow? */
      LUAD_reallocstack(L, ERRORSTACKSIZE);
      LUAG_runerror(L, "stack overflow");
    }
    else
      LUAD_reallocstack(L, newsize);
  }
}


static int stackinuse (LUA_State *L) {
  CallInfo *ci;
  StkId lim = L->top;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    LUA_assert(ci->top <= L->stack_last);
    if (lim < ci->top) lim = ci->top;
  }
  return cast_int(lim - L->stack) + 1;  /* part of stack in use */
}


void LUAD_shrinkstack (LUA_State *L) {
  int inuse = stackinuse(L);
  int goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > LUAI_MAXSTACK) goodsize = LUAI_MAXSTACK;
  if (inuse > LUAI_MAXSTACK ||  /* handling stack overflow? */
      goodsize >= L->stacksize)  /* would grow instead of shrink? */
    condmovestack(L);  /* don't change stack (change only for debugging) */
  else
    LUAD_reallocstack(L, goodsize);  /* shrink it */
}


void LUAD_hook (LUA_State *L, int event, int line) {
  LUA_Hook hook = L->hook;
  if (hook && L->allowhook) {
    CallInfo *ci = L->ci;
    ptrdiff_t top = savestack(L, L->top);
    ptrdiff_t ci_top = savestack(L, ci->top);
    LUA_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    LUAD_checkstack(L, LUA_MINSTACK);  /* ensure minimum stack size */
    ci->top = L->top + LUA_MINSTACK;
    LUA_assert(ci->top <= L->stack_last);
    L->allowhook = 0;  /* cannot call hooks inside a hook */
    ci->callstatus |= CIST_HOOKED;
    LUA_unlock(L);
    (*hook)(L, &ar);
    LUA_lock(L);
    LUA_assert(!L->allowhook);
    L->allowhook = 1;
    ci->top = restorestack(L, ci_top);
    L->top = restorestack(L, top);
    ci->callstatus &= ~CIST_HOOKED;
  }
}


static void callhook (LUA_State *L, CallInfo *ci) {
  int hook = LUA_HOOKCALL;
  ci->u.l.savedpc++;  /* hooks assume 'pc' is already incremented */
  if (isLUA(ci->previous) &&
      GET_OPCODE(*(ci->previous->u.l.savedpc - 1)) == OP_TAILCALL) {
    ci->callstatus |= CIST_TAIL;
    hook = LUA_HOOKTAILCALL;
  }
  LUAD_hook(L, hook, -1);
  ci->u.l.savedpc--;  /* correct 'pc' */
}


static StkId adjust_varargs (LUA_State *L, Proto *p, int actual) {
  int i;
  int nfixargs = p->numparams;
  StkId base, fixed;
  LUA_assert(actual >= nfixargs);
  /* move fixed parameters to final position */
  fixed = L->top - actual;  /* first fixed argument */
  base = L->top;  /* final position of first argument */
  for (i=0; i<nfixargs; i++) {
    setobjs2s(L, L->top++, fixed + i);
    setnilvalue(fixed + i);
  }
  return base;
}


static StkId tryfuncTM (LUA_State *L, StkId func) {
  const TValue *tm = LUAT_gettmbyobj(L, func, TM_CALL);
  StkId p;
  ptrdiff_t funcr = savestack(L, func);
  if (!ttisfunction(tm))
    LUAG_typeerror(L, func, "call");
  /* Open a hole inside the stack at `func' */
  for (p = L->top; p > func; p--) setobjs2s(L, p, p-1);
  incr_top(L);
  func = restorestack(L, funcr);  /* previous call may change stack */
  setobj2s(L, func, tm);  /* tag method is the new function to be called */
  return func;
}



#define next_ci(L) (L->ci = (L->ci->next ? L->ci->next : LUAE_extendCI(L)))


/*
** returns true if function has been executed (C function)
*/
int LUAD_precall (LUA_State *L, StkId func, int nresults) {
  LUA_CFunction f;
  CallInfo *ci;
  int n;  /* number of arguments (LUA) or returns (C) */
  ptrdiff_t funcr = savestack(L, func);
  switch (ttype(func)) {
    case LUA_TLCF:  /* light C function */
      f = fvalue(func);
      goto Cfunc;
    case LUA_TCCL: {  /* C closure */
      f = clCvalue(func)->f;
     Cfunc:
      LUAD_checkstack(L, LUA_MINSTACK);  /* ensure minimum stack size */
      ci = next_ci(L);  /* now 'enter' new function */
      ci->nresults = nresults;
      ci->func = restorestack(L, funcr);
      ci->top = L->top + LUA_MINSTACK;
      LUA_assert(ci->top <= L->stack_last);
      ci->callstatus = 0;
      LUAC_checkGC(L);  /* stack grow uses memory */
      if (L->hookmask & LUA_MASKCALL)
        LUAD_hook(L, LUA_HOOKCALL, -1);
      LUA_unlock(L);
      n = (*f)(L);  /* do the actual call */
      LUA_lock(L);
      api_checknelems(L, n);
      LUAD_poscall(L, L->top - n);
      return 1;
    }
    case LUA_TLCL: {  /* LUA function: prepare its call */
      StkId base;
      Proto *p = clLvalue(func)->p;
      LUAD_checkstack(L, p->maxstacksize);
      func = restorestack(L, funcr);
      n = cast_int(L->top - func) - 1;  /* number of real arguments */
      for (; n < p->numparams; n++)
        setnilvalue(L->top++);  /* complete missing arguments */
      base = (!p->is_vararg) ? func + 1 : adjust_varargs(L, p, n);
      ci = next_ci(L);  /* now 'enter' new function */
      ci->nresults = nresults;
      ci->func = func;
      ci->u.l.base = base;
      ci->top = base + p->maxstacksize;
      LUA_assert(ci->top <= L->stack_last);
      ci->u.l.savedpc = p->code;  /* starting point */
      ci->callstatus = CIST_LUA;
      L->top = ci->top;
      LUAC_checkGC(L);  /* stack grow uses memory */
      if (L->hookmask & LUA_MASKCALL)
        callhook(L, ci);
      return 0;
    }
    default: {  /* not a function */
      func = tryfuncTM(L, func);  /* retry with 'function' tag method */
      return LUAD_precall(L, func, nresults);  /* now it must be a function */
    }
  }
}


int LUAD_poscall (LUA_State *L, StkId firstResult) {
  StkId res;
  int wanted, i;
  CallInfo *ci = L->ci;
  if (L->hookmask & (LUA_MASKRET | LUA_MASKLINE)) {
    if (L->hookmask & LUA_MASKRET) {
      ptrdiff_t fr = savestack(L, firstResult);  /* hook may change stack */
      LUAD_hook(L, LUA_HOOKRET, -1);
      firstResult = restorestack(L, fr);
    }
    L->oldpc = ci->previous->u.l.savedpc;  /* 'oldpc' for caller function */
  }
  res = ci->func;  /* res == final position of 1st result */
  wanted = ci->nresults;
  L->ci = ci = ci->previous;  /* back to caller */
  /* move results to correct place */
  for (i = wanted; i != 0 && firstResult < L->top; i--)
    setobjs2s(L, res++, firstResult++);
  while (i-- > 0)
    setnilvalue(res++);
  L->top = res;
  return (wanted - LUA_MULTRET);  /* 0 iff wanted == LUA_MULTRET */
}


/*
** Call a function (C or LUA). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
*/
void LUAD_call (LUA_State *L, StkId func, int nResults, int allowyield) {
  if (++L->nCcalls >= LUAI_MAXCCALLS) {
    if (L->nCcalls == LUAI_MAXCCALLS)
      LUAG_runerror(L, "C stack overflow");
    else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3)))
      LUAD_throw(L, LUA_ERRERR);  /* error while handing stack error */
  }
  if (!allowyield) L->nny++;
  if (!LUAD_precall(L, func, nResults))  /* is a LUA function? */
    LUAV_execute(L);  /* call it */
  if (!allowyield) L->nny--;
  L->nCcalls--;
}


static void finishCcall (LUA_State *L) {
  CallInfo *ci = L->ci;
  int n;
  LUA_assert(ci->u.c.k != NULL);  /* must have a continuation */
  LUA_assert(L->nny == 0);
  if (ci->callstatus & CIST_YPCALL) {  /* was inside a pcall? */
    ci->callstatus &= ~CIST_YPCALL;  /* finish 'LUA_pcall' */
    L->errfunc = ci->u.c.old_errfunc;
  }
  /* finish 'LUA_callk'/'LUA_pcall' */
  adjustresults(L, ci->nresults);
  /* call continuation function */
  if (!(ci->callstatus & CIST_STAT))  /* no call status? */
    ci->u.c.status = LUA_YIELD;  /* 'default' status */
  LUA_assert(ci->u.c.status != LUA_OK);
  ci->callstatus = (ci->callstatus & ~(CIST_YPCALL | CIST_STAT)) | CIST_YIELDED;
  LUA_unlock(L);
  n = (*ci->u.c.k)(L);
  LUA_lock(L);
  api_checknelems(L, n);
  /* finish 'LUAD_precall' */
  LUAD_poscall(L, L->top - n);
}


static void unroll (LUA_State *L, void *ud) {
  UNUSED(ud);
  for (;;) {
    if (L->ci == &L->base_ci)  /* stack is empty? */
      return;  /* coroutine finished normally */
    if (!isLUA(L->ci))  /* C function? */
      finishCcall(L);
    else {  /* LUA function */
      LUAV_finishOp(L);  /* finish interrupted instruction */
      LUAV_execute(L);  /* execute down to higher C 'boundary' */
    }
  }
}


/*
** check whether thread has a suspended protected call
*/
static CallInfo *findpcall (LUA_State *L) {
  CallInfo *ci;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}


static int recover (LUA_State *L, int status) {
  StkId oldtop;
  CallInfo *ci = findpcall(L);
  if (ci == NULL) return 0;  /* no recovery point */
  /* "finish" LUAD_pcall */
  oldtop = restorestack(L, ci->extra);
  LUAF_close(L, oldtop);
  seterrorobj(L, status, oldtop);
  L->ci = ci;
  L->allowhook = ci->u.c.old_allowhook;
  L->nny = 0;  /* should be zero to be yieldable */
  LUAD_shrinkstack(L);
  L->errfunc = ci->u.c.old_errfunc;
  ci->callstatus |= CIST_STAT;  /* call has error status */
  ci->u.c.status = status;  /* (here it is) */
  return 1;  /* continue running the coroutine */
}


/*
** signal an error in the call to 'resume', not in the execution of the
** coroutine itself. (Such errors should not be handled by any coroutine
** error handler and should not kill the coroutine.)
*/
static l_noret resume_error (LUA_State *L, const char *msg, StkId firstArg) {
  L->top = firstArg;  /* remove args from the stack */
  setsvalue2s(L, L->top, LUAS_new(L, msg));  /* push error message */
  api_incr_top(L);
  LUAD_throw(L, -1);  /* jump back to 'LUA_resume' */
}


/*
** do the work for 'LUA_resume' in protected mode
*/
static void resume (LUA_State *L, void *ud) {
  int nCcalls = L->nCcalls;
  StkId firstArg = cast(StkId, ud);
  CallInfo *ci = L->ci;
  if (nCcalls >= LUAI_MAXCCALLS)
    resume_error(L, "C stack overflow", firstArg);
  if (L->status == LUA_OK) {  /* may be starting a coroutine */
    if (ci != &L->base_ci)  /* not in base level? */
      resume_error(L, "cannot resume non-suspended coroutine", firstArg);
    /* coroutine is in base level; start running it */
    if (!LUAD_precall(L, firstArg - 1, LUA_MULTRET))  /* LUA function? */
      LUAV_execute(L);  /* call it */
  }
  else if (L->status != LUA_YIELD)
    resume_error(L, "cannot resume dead coroutine", firstArg);
  else {  /* resuming from previous yield */
    L->status = LUA_OK;
    ci->func = restorestack(L, ci->extra);
    if (isLUA(ci))  /* yielded inside a hook? */
      LUAV_execute(L);  /* just continue running LUA code */
    else {  /* 'common' yield */
      if (ci->u.c.k != NULL) {  /* does it have a continuation? */
        int n;
        ci->u.c.status = LUA_YIELD;  /* 'default' status */
        ci->callstatus |= CIST_YIELDED;
        LUA_unlock(L);
        n = (*ci->u.c.k)(L);  /* call continuation */
        LUA_lock(L);
        api_checknelems(L, n);
        firstArg = L->top - n;  /* yield results come from continuation */
      }
      LUAD_poscall(L, firstArg);  /* finish 'LUAD_precall' */
    }
    unroll(L, NULL);
  }
  LUA_assert(nCcalls == L->nCcalls);
}


LUA_API int LUA_resume (LUA_State *L, LUA_State *from, int nargs) {
  int status;
  LUA_lock(L);
  LUAi_userstateresume(L, nargs);
  L->nCcalls = (from) ? from->nCcalls + 1 : 1;
  L->nny = 0;  /* allow yields */
  api_checknelems(L, (L->status == LUA_OK) ? nargs + 1 : nargs);
  status = LUAD_rawrunprotected(L, resume, L->top - nargs);
  if (status == -1)  /* error calling 'LUA_resume'? */
    status = LUA_ERRRUN;
  else {  /* yield or regular error */
    while (status != LUA_OK && status != LUA_YIELD) {  /* error? */
      if (recover(L, status))  /* recover point? */
        status = LUAD_rawrunprotected(L, unroll, NULL);  /* run continuation */
      else {  /* unrecoverable error */
        L->status = cast_byte(status);  /* mark thread as `dead' */
        seterrorobj(L, status, L->top);
        L->ci->top = L->top;
        break;
      }
    }
    LUA_assert(status == L->status);
  }
  L->nny = 1;  /* do not allow yields */
  L->nCcalls--;
  LUA_assert(L->nCcalls == ((from) ? from->nCcalls : 0));
  LUA_unlock(L);
  return status;
}


LUA_API int LUA_yieldk (LUA_State *L, int nresults, int ctx, LUA_CFunction k) {
  CallInfo *ci = L->ci;
  LUAi_userstateyield(L, nresults);
  LUA_lock(L);
  api_checknelems(L, nresults);
  if (L->nny > 0) {
    if (L != G(L)->mainthread)
      LUAG_runerror(L, "attempt to yield across a C-call boundary");
    else
      LUAG_runerror(L, "attempt to yield from outside a coroutine");
  }
  L->status = LUA_YIELD;
  ci->extra = savestack(L, ci->func);  /* save current 'func' */
  if (isLUA(ci)) {  /* inside a hook? */
    api_check(L, k == NULL, "hooks cannot continue after yielding");
  }
  else {
    if ((ci->u.c.k = k) != NULL)  /* is there a continuation? */
      ci->u.c.ctx = ctx;  /* save context */
    ci->func = L->top - nresults - 1;  /* protect stack below results */
    LUAD_throw(L, LUA_YIELD);
  }
  LUA_assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  LUA_unlock(L);
  return 0;  /* return to 'LUAD_hook' */
}


int LUAD_pcall (LUA_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  unsigned short old_nny = L->nny;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = LUAD_rawrunprotected(L, func, u);
  if (status != LUA_OK) {  /* an error occurred? */
    StkId oldtop = restorestack(L, old_top);
    LUAF_close(L, oldtop);  /* close possible pending closures */
    seterrorobj(L, status, oldtop);
    L->ci = old_ci;
    L->allowhook = old_allowhooks;
    L->nny = old_nny;
    LUAD_shrinkstack(L);
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
** Execute a protected parser.
*/
struct SParser {  /* data to `f_parser' */
  ZIO *z;
  Mbuffer buff;  /* dynamic structure used by the scanner */
  Dyndata dyd;  /* dynamic structures used by the parser */
  const char *mode;
  const char *name;
};


static void checkmode (LUA_State *L, const char *mode, const char *x) {
  if (mode && strchr(mode, x[0]) == NULL) {
    LUAO_pushfstring(L,
       "attempt to load a %s chunk (mode is " LUA_QS ")", x, mode);
    LUAD_throw(L, LUA_ERRSYNTAX);
  }
}


static void f_parser (LUA_State *L, void *ud) {
  int i;
  Closure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  int c = zgetc(p->z);  /* read first character */
  if (c == LUA_SIGNATURE[0]) {
    checkmode(L, p->mode, "binary");
    cl = LUAU_undump(L, p->z, &p->buff, p->name);
  }
  else {
    checkmode(L, p->mode, "text");
    cl = LUAY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  LUA_assert(cl->l.nupvalues == cl->l.p->sizeupvalues);
  for (i = 0; i < cl->l.nupvalues; i++) {  /* initialize upvalues */
    UpVal *up = LUAF_newupval(L);
    cl->l.upvals[i] = up;
    LUAC_objbarrier(L, cl, up);
  }
}


int LUAD_protectedparser (LUA_State *L, ZIO *z, const char *name,
                                        const char *mode) {
  struct SParser p;
  int status;
  L->nny++;  /* cannot yield during parsing */
  p.z = z; p.name = name; p.mode = mode;
  p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; p.dyd.label.size = 0;
  LUAZ_initbuffer(L, &p.buff);
  status = LUAD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc);
  LUAZ_freebuffer(L, &p.buff);
  LUAM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
  LUAM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
  LUAM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
  L->nny--;
  return status;
}


