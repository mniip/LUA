/*
** $Id: lparser.c,v 2.130 2013/02/06 13:37:39 roberto Exp $
** LUA Parser
** See Copyright Notice in LUA.h
*/


#include <string.h>

#define lparser_c
#define LUA_CORE

#include "LUA.h"

#include "Lcode.h"
#include "Ldebug.h"
#include "Ldo.h"
#include "Lfunc.h"
#include "Llex.h"
#include "Lmem.h"
#include "Lobject.h"
#include "Lopcodes.h"
#include "Lparser.h"
#include "Lstate.h"
#include "Lstring.h"
#include "Ltable.h"



/* maximum number of local variables per function (must be smaller
   than 250, due to the bytecode format) */
#define MAXVARS		200


#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)



/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* chain */
  short firstcomefrom;  /* index of first comefrom in this block */
  short firstlabel;  /* index of first pending label in this block */
  lu_byte nactvar;  /* # active locals outside the block */
  lu_byte upval;  /* true if some variable in the block is an upvalue */
  lu_byte isloop;  /* true if `block' is a loop */
} BlockCnt;



/*
** prototypes for recursive non-terminal functions
*/
static void statement (LexState *ls);
static void expr (LexState *ls, expdesc *v);


static void anchor_token (LexState *ls) {
  /* last token from outer function must be EOS */
  LUA_assert(ls->fs != NULL || ls->t.token == TK_EOS);
  if (ls->t.token == TK_NAME || ls->t.token == TK_STRING) {
    TString *ts = ls->t.seminfo.ts;
    LUAX_newstring(ls, getstr(ts), ts->tsv.len);
  }
}


/* semantic error */
static l_noret semerror (LexState *ls, const char *msg) {
  ls->t.token = 0;  /* remove 'near to' from final message */
  LUAX_syntaxerror(ls, msg);
}


static l_noret error_expected (LexState *ls, int token) {
  LUAX_syntaxerror(ls,
      LUAO_pushfstring(ls->L, "%s expected", LUAX_token2str(ls, token)));
}


static l_noret errorlimit (FuncState *fs, int limit, const char *what) {
  LUA_State *L = fs->ls->L;
  const char *msg;
  int line = fs->f->linedefined;
  const char *where = (line == 0)
                      ? "main function"
                      : LUAO_pushfstring(L, "function at line %d", line);
  msg = LUAO_pushfstring(L, "too many %s (limit is %d) in %s",
                             what, limit, where);
  LUAX_syntaxerror(fs->ls, msg);
}


static void checklimit (FuncState *fs, int v, int l, const char *what) {
  if (v > l) errorlimit(fs, l, what);
}


static int testnext (LexState *ls, int c) {
  if (ls->t.token == c) {
    LUAX_next(ls);
    return 1;
  }
  else return 0;
}


static void check (LexState *ls, int c) {
  if (ls->t.token != c)
    error_expected(ls, c);
}


static void checknext (LexState *ls, int c) {
  check(ls, c);
  LUAX_next(ls);
}


#define check_condition(ls,c,msg)	{ if (!(c)) LUAX_syntaxerror(ls, msg); }



static void check_match (LexState *ls, int what, int who, int where) {
  if (!testnext(ls, what)) {
    if (where == ls->linenumber)
      error_expected(ls, what);
    else {
      LUAX_syntaxerror(ls, LUAO_pushfstring(ls->L,
             "%s expected (to close %s at line %d)",
              LUAX_token2str(ls, what), LUAX_token2str(ls, who), where));
    }
  }
}


static TString *str_checkname (LexState *ls) {
  if(ls->t.token == TK_NAME)
  {
    TString *ts = ls->t.seminfo.ts;
    LUAX_next(ls);
    return ts;
  }
  return LUAS_new(ls->L, "");
}


static void init_exp (expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->u.info = i;
}


static void codestring (LexState *ls, expdesc *e, TString *s) {
  init_exp(e, VK, LUAK_stringK(ls->fs, s));
}


static void checkname (LexState *ls, expdesc *e) {
  codestring(ls, e, str_checkname(ls));
}


static int registerlocalvar (LexState *ls, TString *varname) {
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int oldsize = f->sizelocvars;
  LUAM_growvector(ls->L, f->locvars, fs->nlocvars, f->sizelocvars,
                  LocVar, SHRT_MAX, "local variables");
  while (oldsize < f->sizelocvars) f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->nlocvars].varname = varname;
  LUAC_objbarrier(ls->L, f, varname);
  return fs->nlocvars++;
}


static void new_localvar (LexState *ls, TString *name) {
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  int reg = registerlocalvar(ls, name);
  checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal,
                  MAXVARS, "local variables");
  LUAM_growvector(ls->L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, MAX_INT, "local variables");
  dyd->actvar.arr[dyd->actvar.n++].idx = cast(short, reg);
}


static void new_localvarliteral_ (LexState *ls, const char *name, size_t sz) {
  new_localvar(ls, LUAX_newstring(ls, name, sz));
}

#define new_localvarliteral(ls,v) \
    new_localvarliteral_(ls, "" v, (sizeof(v)/sizeof(char))-1)


static LocVar *getlocvar (FuncState *fs, int i) {
  int idx = fs->ls->dyd->actvar.arr[fs->firstlocal + i].idx;
  LUA_assert(idx < fs->nlocvars);
  return &fs->f->locvars[idx];
}


static void adjustlocalvars (LexState *ls, int nvars) {
  FuncState *fs = ls->fs;
  fs->nactvar = cast_byte(fs->nactvar + nvars);
  for (; nvars; nvars--) {
    getlocvar(fs, fs->nactvar - nvars)->startpc = fs->pc;
  }
}


static void removevars (FuncState *fs, int tolevel) {
  fs->ls->dyd->actvar.n -= (fs->nactvar - tolevel);
  while (fs->nactvar > tolevel)
    getlocvar(fs, --fs->nactvar)->endpc = fs->pc;
}


static int searchupvalue (FuncState *fs, TString *name) {
  int i;
  Upvaldesc *up = fs->f->upvalues;
  for (i = 0; i < fs->nups; i++) {
    if (LUAS_eqstr(up[i].name, name)) return i;
  }
  return -1;  /* not found */
}


static int newupvalue (FuncState *fs, TString *name, expdesc *v) {
  Proto *f = fs->f;
  int oldsize = f->sizeupvalues;
  checklimit(fs, fs->nups + 1, MAXUPVAL, "upvalues");
  LUAM_growvector(fs->ls->L, f->upvalues, fs->nups, f->sizeupvalues,
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < f->sizeupvalues) f->upvalues[oldsize++].name = NULL;
  f->upvalues[fs->nups].instack = (v->k == VLOCAL);
  f->upvalues[fs->nups].idx = cast_byte(v->u.info);
  f->upvalues[fs->nups].name = name;
  LUAC_objbarrier(fs->ls->L, f, name);
  return fs->nups++;
}


static int searchvar (FuncState *fs, TString *n) {
  int i;
  for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
    if (LUAS_eqstr(n, getlocvar(fs, i)->varname))
      return i;
  }
  return -1;  /* not found */
}


/*
  Mark block where variable at given level was defined
  (to emit close instructions later).
*/
static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level) bl = bl->previous;
  bl->upval = 1;
}


/*
  Find variable with given name 'n'. If it is an upvalue, add this
  upvalue into all intermediate functions.
*/
static int singlevaraux (FuncState *fs, TString *n, expdesc *var, int base, int mindepth) {
  if (fs == NULL)  /* no more levels? */
    return VVOID;  /* default is global */
  else if (mindepth == 0) {
    int v = searchvar(fs, n);  /* look up locals at current level */
    if (v >= 0) {  /* found? */
      init_exp(var, VLOCAL, v);  /* variable is local */
      if (!base)
        markupval(fs, v);  /* local will be used as an upval */
      return VLOCAL;
    }
  }
  /* not found as local at current level, or level insufficient; try upvalues */
  if(mindepth == 0)
  {
    int idx = searchupvalue(fs, n); /* try existing upvalues */
    if (idx >= 0) {  /* found? */
      init_exp(var, VUPVAL, idx);
      return VUPVAL;
    }
  }
  if (singlevaraux(fs->prev, n, var, 0, mindepth > 0 ? mindepth - 1 : 0) == VVOID) /* try upper levels */
    return VVOID;  /* not found; is a global */
  int idx  = newupvalue(fs, n, var);  /* will be a new upvalue */
  init_exp(var, VUPVAL, idx);
  return VUPVAL;
}


static void singlevar (LexState *ls, expdesc *var) {
  int mindepth = 0;
  while(ls->t.token == '@') {
    LUAX_next(ls);
    mindepth++;
  }
  FuncState *fs = ls->fs;
  TString *varname = str_checkname(ls);
  if (singlevaraux(fs, varname, var, 0, mindepth) == VVOID) {  /* global name? */
    expdesc key;
    singlevaraux(fs, ls->envn, var, 1, 0);  /* get environment variable */
    LUA_assert(var->k == VLOCAL || var->k == VUPVAL);
    codestring(ls, &key, varname);  /* key is variable name */
    LUAK_indexed(fs, var, &key);  /* env[varname] */
  }
}


static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
  FuncState *fs = ls->fs;
  int extra = nvars - nexps;
  if (hasmultret(e->k)) {
    extra++;  /* includes call itself */
    if (extra < 0) extra = 0;
    LUAK_setreturns(fs, e, extra);  /* last exp. provides the difference */
    if (extra > 1) LUAK_reserveregs(fs, extra-1);
  }
  else {
    if (e->k != VVOID) LUAK_exp2nextreg(fs, e);  /* close last expression */
    if (extra > 0) {
      int reg = fs->freereg;
      LUAK_reserveregs(fs, extra);
      LUAK_nil(fs, reg, extra);
    }
  }
}


static void enterlevel (LexState *ls) {
  LUA_State *L = ls->L;
  ++L->nCcalls;
  checklimit(ls->fs, L->nCcalls, LUAI_MAXCCALLS, "C levels");
}


#define leavelevel(ls)	((ls)->L->nCcalls--)


static void closelabel (LexState *ls, int g, Labeldesc *comefrom) {
  int i;
  FuncState *fs = ls->fs;
  Labellist *gl = &ls->dyd->gt;
  Labeldesc *gt = &gl->arr[g];
  LUA_assert(LUAS_eqstr(gt->name, comefrom->name));
  if (gt->nactvar < comefrom->nactvar) {
    TString *vname = getlocvar(fs, gt->nactvar)->varname;
    const char *msg = LUAO_pushfstring(ls->L,
      "<label %s> at line %d jumps into the scope of local " LUA_QS,
      getstr(gt->name), gt->line, getstr(vname));
    semerror(ls, msg);
  }
  LUAK_patchlist(fs, gt->pc, comefrom->pc);
  /* remove label from pending list */
  for (i = g; i < gl->n - 1; i++)
    gl->arr[i] = gl->arr[i + 1];
  gl->n--;
}


/*
** try to close a label with existing comefroms; this solves backward jumps
*/
static int findcomefrom (LexState *ls, int g) {
  int i;
  BlockCnt *bl = ls->fs->bl;
  Dyndata *dyd = ls->dyd;
  Labeldesc *gt = &dyd->gt.arr[g];
  /* check comefroms in current block for a match */
  for (i = bl->firstcomefrom; i < dyd->comefrom.n; i++) {
    Labeldesc *lb = &dyd->comefrom.arr[i];
    if (LUAS_eqstr(lb->name, gt->name)) {  /* correct comefrom? */
      if (gt->nactvar > lb->nactvar &&
          (bl->upval || dyd->comefrom.n > bl->firstcomefrom))
        LUAK_patchclose(ls->fs, gt->pc, lb->nactvar);
      closelabel(ls, g, lb);  /* close it */
      return 1;
    }
  }
  return 0;  /* comefrom not found; cannot close label */
}


static int newcomefromentry (LexState *ls, Labellist *l, TString *name,
                          int line, int pc) {
  int n = l->n;
  LUAM_growvector(ls->L, l->arr, n, l->size,
                  Labeldesc, SHRT_MAX, "comefroms/labels");
  l->arr[n].name = name;
  l->arr[n].line = line;
  l->arr[n].nactvar = ls->fs->nactvar;
  l->arr[n].pc = pc;
  l->n++;
  return n;
}


/*
** check whether new comefrom 'lb' matches any pending labels in current
** block; solves forward jumps
*/
static void findlabels (LexState *ls, Labeldesc *lb) {
  Labellist *gl = &ls->dyd->gt;
  int i = ls->fs->bl->firstlabel;
  while (i < gl->n) {
    if (LUAS_eqstr(gl->arr[i].name, lb->name))
      closelabel(ls, i, lb);
    else
      i++;
  }
}


/*
** "export" pending labels to outer level, to check them against
** outer comefroms; if the block being exited has upvalues, and
** the label exits the scope of any variable (which can be the
** upvalue), close those variables being exited.
*/
static void movelabelsout (FuncState *fs, BlockCnt *bl) {
  int i = bl->firstlabel;
  Labellist *gl = &fs->ls->dyd->gt;
  /* correct pending labels to current block and try to close it
     with visible comefroms */
  while (i < gl->n) {
    Labeldesc *gt = &gl->arr[i];
    if (gt->nactvar > bl->nactvar) {
      if (bl->upval)
        LUAK_patchclose(fs, gt->pc, bl->nactvar);
      gt->nactvar = bl->nactvar;
    }
    if (!findcomefrom(fs->ls, i))
      i++;  /* move to next one */
  }
}


static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->nactvar;
  bl->firstcomefrom = fs->ls->dyd->comefrom.n;
  bl->firstlabel = fs->ls->dyd->gt.n;
  bl->upval = 0;
  bl->previous = fs->bl;
  fs->bl = bl;
  LUA_assert(fs->freereg == fs->nactvar);
}


/*
** create a comefrom named "break" to resolve break statements
*/
static void breakcomefrom (LexState *ls) {
  TString *n = LUAS_new(ls->L, "BREAK");
  int l = newcomefromentry(ls, &ls->dyd->comefrom, n, 0, ls->fs->pc);
  findlabels(ls, &ls->dyd->comefrom.arr[l]);
}

/*
** generates an error for an undefined 'label'; choose appropriate
** message when comefrom name is a reserved word (which can only be 'break')
*/
static l_noret undeflabel (LexState *ls, Labeldesc *gt) {
  const char *msg = isreserved(gt->name)
                    ? "<%s> at line %d not inside a loop"
                    : "no visible comefrom " LUA_QS " for <label> at line %d";
  msg = LUAO_pushfstring(ls->L, msg, getstr(gt->name), gt->line);
  semerror(ls, msg);
}


static void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  LexState *ls = fs->ls;
  if (bl->previous && bl->upval) {
    /* create a 'jump to here' to close upvalues */
    int j = LUAK_jump(fs);
    LUAK_patchclose(fs, j, bl->nactvar);
    LUAK_patchtohere(fs, j);
  }
  if (bl->isloop)
    breakcomefrom(ls);  /* close pending breaks */
  fs->bl = bl->previous;
  removevars(fs, bl->nactvar);
  LUA_assert(bl->nactvar == fs->nactvar);
  fs->freereg = fs->nactvar;  /* free registers */
  ls->dyd->comefrom.n = bl->firstcomefrom;  /* remove local comefroms */
  if (bl->previous)  /* inner block? */
    movelabelsout(fs, bl);  /* update pending labels to outer block */
  else if (bl->firstlabel < ls->dyd->gt.n)  /* pending labels in outer block? */
    undeflabel(ls, &ls->dyd->gt.arr[bl->firstlabel]);  /* error */
}


/*
** adds a new prototype into list of prototypes
*/
static Proto *addprototype (LexState *ls) {
  Proto *clp;
  LUA_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;  /* prototype of current function */
  if (fs->np >= f->sizep) {
    int oldsize = f->sizep;
    LUAM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->sizep) f->p[oldsize++] = NULL;
  }
  f->p[fs->np++] = clp = LUAF_newproto(L);
  LUAC_objbarrier(L, f, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction must use the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.
*/
static void codeclosure (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs->prev;
  init_exp(v, VRELOCABLE, LUAK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
  LUAK_exp2nextreg(fs, v);  /* fix it at the last register */
}


static void open_func (LexState *ls, FuncState *fs, BlockCnt *bl) {
  LUA_State *L = ls->L;
  Proto *f;
  fs->prev = ls->fs;  /* linked list of funcstates */
  fs->ls = ls;
  ls->fs = fs;
  fs->pc = 0;
  fs->lasttarget = 0;
  fs->jpc = NO_JUMP;
  fs->freereg = 0;
  fs->nk = 0;
  fs->np = 0;
  fs->nups = 0;
  fs->nlocvars = 0;
  fs->nactvar = 0;
  fs->firstlocal = ls->dyd->actvar.n;
  fs->bl = NULL;
  f = fs->f;
  f->source = ls->source;
  f->maxstacksize = 2;  /* registers 0/1 are always valid */
  fs->h = LUAH_new(L);
  /* anchor table of constants (to avoid being collected) */
  sethvalue2s(L, L->top, fs->h);
  incr_top(L);
  enterblock(fs, bl, 0);
}


static void close_func (LexState *ls) {
  LUA_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  LUAK_ret(fs, 0, 0);  /* final return */
  leaveblock(fs);
  LUAM_reallocvector(L, f->code, f->sizecode, fs->pc, Instruction);
  f->sizecode = fs->pc;
  LUAM_reallocvector(L, f->lineinfo, f->sizelineinfo, fs->pc, int);
  f->sizelineinfo = fs->pc;
  LUAM_reallocvector(L, f->k, f->sizek, fs->nk, TValue);
  f->sizek = fs->nk;
  LUAM_reallocvector(L, f->p, f->sizep, fs->np, Proto *);
  f->sizep = fs->np;
  LUAM_reallocvector(L, f->locvars, f->sizelocvars, fs->nlocvars, LocVar);
  f->sizelocvars = fs->nlocvars;
  LUAM_reallocvector(L, f->upvalues, f->sizeupvalues, fs->nups, Upvaldesc);
  f->sizeupvalues = fs->nups;
  LUA_assert(fs->bl == NULL);
  ls->fs = fs->prev;
  /* last token read was anchored in defunct function; must re-anchor it */
  anchor_token(ls);
  L->top--;  /* pop table of constants */
  LUAC_checkGC(L);
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it handled in separate.
*/
static int block_follow (LexState *ls, int withuntil) {
  switch (ls->t.token) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END: case TK_EOS:
      return 1;
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


static void statlist (LexState *ls) {
  /* statlist -> { stat [`;'] } */
  while (!block_follow(ls, 1)) {
    if (ls->t.token == TK_RETURN) {
      statement(ls);
      return;  /* 'return' must be last statement */
    }
    statement(ls);
  }
}


static void fieldsel (LexState *ls, expdesc *v) {
  /* fieldsel -> ['.' | ':'] NAME */
  FuncState *fs = ls->fs;
  expdesc key;
  LUAK_exp2anyregup(fs, v);
  LUAX_next(ls);  /* skip the dot or colon */
  checkname(ls, &key);
  LUAK_indexed(fs, v, &key);
}


static void yindex (LexState *ls, expdesc *v) {
  /* index -> '[' expr ']' */
  LUAX_next(ls);  /* skip the '[' */
  expr(ls, v);
  LUAK_exp2val(ls->fs, v);
  checknext(ls, ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of `record' elements */
  int na;  /* total number of array elements */
  int tostore;  /* number of array elements pending to be stored */
};


static void recfield (LexState *ls, struct ConsControl *cc) {
  /* recfield -> (NAME | `['exp1`]') = exp1 */
  FuncState *fs = ls->fs;
  int reg = ls->fs->freereg;
  expdesc key, val;
  int rkkey;
  if (ls->t.token != '[') {
    checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
    checkname(ls, &key);
  }
  else  /* ls->t.token != TK_NAME */
    yindex(ls, &key);
  cc->nh++;
  checknext(ls, '=');
  rkkey = LUAK_exp2RK(fs, &key);
  expr(ls, &val);
  LUAK_codeABC(fs, OP_SETTABLE, cc->t->u.info, rkkey, LUAK_exp2RK(fs, &val));
  fs->freereg = reg;  /* free registers */
}


static void closelistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->v.k == VVOID) return;  /* there is no list item */
  LUAK_exp2nextreg(fs, &cc->v);
  cc->v.k = VVOID;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    LUAK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);  /* flush */
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.k)) {
    LUAK_setmultret(fs, &cc->v);
    LUAK_setlist(fs, cc->t->u.info, cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.k != VVOID)
      LUAK_exp2nextreg(fs, &cc->v);
    LUAK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);
  }
}


static void listfield (LexState *ls, struct ConsControl *cc) {
  /* listfield -> exp */
  expr(ls, &cc->v);
  checklimit(ls->fs, cc->na, MAX_INT, "items in a constructor");
  cc->na++;
  cc->tostore++;
}


static void field (LexState *ls, struct ConsControl *cc) {
  /* field -> listfield | recfield */
  switch(ls->t.token) {
    case TK_NAME: {  /* may be 'listfield' or 'recfield' */
      if (LUAX_lookahead(ls) != '=' && ls->t.token != '=')  /* expression? */
        listfield(ls, cc);
      else
        recfield(ls, cc);
      break;
    }
    case '=':
    case '[': {
      recfield(ls, cc);
      break;
    }
    default: {
      listfield(ls, cc);
      break;
    }
  }
}


static void constructor (LexState *ls, expdesc *t) {
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  int pc = LUAK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
  struct ConsControl cc;
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  init_exp(t, VRELOCABLE, pc);
  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
  LUAK_exp2nextreg(ls->fs, t);  /* fix it at stack top */
  checknext(ls, '{');
  do {
    LUA_assert(cc.v.k == VVOID || cc.tostore > 0);
    if (ls->t.token == '}') break;
    closelistfield(fs, &cc);
    field(ls, &cc);
  } while (testnext(ls, ',') || testnext(ls, ';'));
  check_match(ls, '}', '{', line);
  lastlistfield(fs, &cc);
  SETARG_B(fs->f->code[pc], LUAO_int2fb(cc.na)); /* set initial array size */
  SETARG_C(fs->f->code[pc], LUAO_int2fb(cc.nh));  /* set initial table size */
}

/* }====================================================================== */



static void parlist (LexState *ls) {
  /* parlist -> [ param { `,' param } ] */
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int nparams = 0;
  f->is_vararg = 0;
  if (ls->t.token != ')') {  /* is `parlist' not empty? */
    do {
      switch (ls->t.token) {
        case TK_NAME: {  /* param -> NAME */
          new_localvar(ls, str_checkname(ls));
          nparams++;
          break;
        }
        case TK_DOTS: {  /* param -> `...' */
          LUAX_next(ls);
          f->is_vararg = 1;
          break;
        }
        default: LUAX_syntaxerror(ls, "<name> or " LUA_QL("...") " expected");
      }
    } while (!f->is_vararg && testnext(ls, ','));
  }
  adjustlocalvars(ls, nparams);
  f->numparams = cast_byte(fs->nactvar);
  LUAK_reserveregs(fs, fs->nactvar);  /* reserve register for parameters */
}


static void body (LexState *ls, expdesc *e, int ismethod, int line) {
  /* body ->  `(' parlist `)' block END */
  FuncState new_fs;
  BlockCnt bl;
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);
  checknext(ls, '(');
  if (ismethod) {
    new_localvarliteral(ls, "SELF");  /* create 'self' parameter */
    adjustlocalvars(ls, 1);
  }
  parlist(ls);
  checknext(ls, ')');
  statlist(ls);
  new_fs.f->lastlinedefined = ls->linenumber;
  check_match(ls, TK_END, TK_FUNCTION, line);
  codeclosure(ls, e);
  close_func(ls);
}


static void simplebody (LexState *ls, expdesc *e, int line) {
  /* simplebody -> block END */
  FuncState new_fs;
  BlockCnt bl;
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);
  new_localvarliteral(ls, "");
  adjustlocalvars(ls, 1);
  new_fs.f->numparams = cast_byte(new_fs.nactvar);
  new_fs.f->is_vararg = 1;
  LUAK_reserveregs(&new_fs, new_fs.nactvar);
  statlist(ls);
  new_fs.f->lastlinedefined = ls->linenumber;
  check_match(ls, TK_END, TK_DO, line);
  codeclosure(ls, e);
  close_func(ls);
}


static int explist (LexState *ls, expdesc *v) {
  /* explist -> expr { `,' expr } */
  int n = 1;  /* at least one expression */
  expr(ls, v);
  while (testnext(ls, ',')) {
    LUAK_exp2nextreg(ls->fs, v);
    expr(ls, v);
    n++;
  }
  return n;
}


static void funcargs (LexState *ls, expdesc *f, int line) {
  FuncState *fs = ls->fs;
  expdesc args;
  int base, nparams;
  switch (ls->t.token) {
    case '(': {  /* funcargs -> `(' [ explist ] `)' */
      LUAX_next(ls);
      if (ls->t.token == ')')  /* arg list is empty? */
        args.k = VVOID;
      else {
        explist(ls, &args);
        LUAK_setmultret(fs, &args);
      }
      check_match(ls, ')', '(', line);
      break;
    }
    case '{': {  /* funcargs -> constructor */
      constructor(ls, &args);
      break;
    }
    case TK_STRING: {  /* funcargs -> STRING */
      codestring(ls, &args, ls->t.seminfo.ts);
      LUAX_next(ls);  /* must use `seminfo' before `next' */
      break;
    }
    default: {
      LUAX_syntaxerror(ls, "function arguments expected");
    }
  }
  LUA_assert(f->k == VNONRELOC);
  base = f->u.info;  /* base register for call */
  if (hasmultret(args.k))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.k != VVOID)
      LUAK_exp2nextreg(fs, &args);  /* close last argument */
    nparams = fs->freereg - (base+1);
  }
  init_exp(f, VCALL, LUAK_codeABC(fs, OP_CALL, base, nparams+1, 2));
  LUAK_fixline(fs, line);
  fs->freereg = base+1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void primaryexp (LexState *ls, expdesc *v) {
  /* primaryexp -> NAME | '(' expr ')' */
  switch (ls->t.token) {
    case '(': {
      int line = ls->linenumber;
      LUAX_next(ls);
      expr(ls, v);
      check_match(ls, ')', '(', line);
      LUAK_dischargevars(ls->fs, v);
      return;
    }
    default: {
      singlevar(ls, v);
      return;
    }
  }
}


static void suffixedexp (LexState *ls, expdesc *v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  primaryexp(ls, v);
  for (;;) {
    switch (ls->t.token) {
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* `[' exp1 `]' */
        expdesc key;
        LUAK_exp2anyregup(fs, v);
        yindex(ls, &key);
        LUAK_indexed(fs, v, &key);
        break;
      }
      case ':': {  /* `:' NAME funcargs */
        expdesc key;
        LUAX_next(ls);
        checkname(ls, &key);
        LUAK_self(fs, v, &key);
        funcargs(ls, v, line);
        break;
      }
      case '(': case TK_STRING: case '{': {  /* funcargs */
        LUAK_exp2nextreg(fs, v);
        funcargs(ls, v, line);
        break;
      }
      default: return;
    }
  }
}


static void simpleexp (LexState *ls, expdesc *v) {
  /* simpleexp -> NUMBER | STRING | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | DO block END | suffixedexp */
  switch (ls->t.token) {
    case TK_NUMBER: {
      init_exp(v, VKNUM, 0);
      v->u.nval = ls->t.seminfo.r;
      break;
    }
    case TK_STRING: {
      codestring(ls, v, ls->t.seminfo.ts);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *fs = ls->fs;
      check_condition(ls, fs->f->is_vararg,
                      "cannot use " LUA_QL("...") " outside a vararg function");
      init_exp(v, VVARARG, LUAK_codeABC(fs, OP_VARARG, 0, 1, 0));
      break;
    }
    case '{': {  /* constructor */
      constructor(ls, v);
      return;
    }
    case TK_FUNCTION: {
      LUAX_next(ls);
      body(ls, v, 0, ls->linenumber);
      return;
    }
    case TK_DO: {
      LUAX_next(ls);
      simplebody(ls, v, ls->linenumber);
      return;
    }
    default: {
      suffixedexp(ls, v);
      return;
    }
  }
  LUAX_next(ls);
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '/': return OPR_DIV;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}


static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* `+' `-' `*' `/' `%' */
   {10, 9}, {5, 4},                 /* ^, .. (right associative) */
   {3, 3}, {3, 3}, {3, 3},          /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},          /* ~=, >, >= */
   {2, 2}, {1, 1}                   /* and, or */
};

#define UNARY_PRIORITY	8  /* priority for unary operators */


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where `binop' is any binary operator with a priority higher than `limit'
*/
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {
    int line = ls->linenumber;
    LUAX_next(ls);
    subexpr(ls, v, UNARY_PRIORITY);
    LUAK_prefix(ls->fs, uop, v, line);
  }
  else simpleexp(ls, v);
  /* expand while operators have priorities higher than `limit' */
  op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;
    LUAX_next(ls);
    LUAK_infix(ls->fs, op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(ls, &v2, priority[op].right);
    LUAK_posfix(ls->fs, op, v, &v2, line);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


static void expr (LexState *ls, expdesc *v) {
  subexpr(ls, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static void block (LexState *ls) {
  /* block -> statlist */
  FuncState *fs = ls->fs;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  statlist(ls);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->fs;
  int extra = fs->freereg;  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {  /* check all previous assignments */
    if (lh->v.k == VINDEXED) {  /* assigning to a table? */
      /* table is the upvalue/local being assigned now? */
      if (lh->v.u.ind.vt == v->k && lh->v.u.ind.t == v->u.info) {
        conflict = 1;
        lh->v.u.ind.vt = VLOCAL;
        lh->v.u.ind.t = extra;  /* previous assignment will use safe copy */
      }
      /* index is the local being assigned? (index cannot be upvalue) */
      if (v->k == VLOCAL && lh->v.u.ind.idx == v->u.info) {
        conflict = 1;
        lh->v.u.ind.idx = extra;  /* previous assignment will use safe copy */
      }
    }
  }
  if (conflict) {
    /* copy upvalue/local value to a temporary (in position 'extra') */
    OpCode op = (v->k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
    LUAK_codeABC(fs, op, extra, v->u.info, 0);
    LUAK_reserveregs(fs, 1);
  }
}


static void assignment (LexState *ls, struct LHS_assign *lh, int nvars) {
  expdesc e;
  check_condition(ls, vkisvar(lh->v.k), "syntax error");
  if (testnext(ls, ',')) {  /* assignment -> ',' suffixedexp assignment */
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(ls, &nv.v);
    if (nv.v.k != VINDEXED)
      check_conflict(ls, lh, &nv.v);
    checklimit(ls->fs, nvars + ls->L->nCcalls, LUAI_MAXCCALLS,
                    "C levels");
    assignment(ls, &nv, nvars+1);
  }
  else {  /* assignment -> `=' explist */
    int nexps;
    checknext(ls, '=');
    nexps = explist(ls, &e);
    if (nexps != nvars) {
      adjust_assign(ls, nvars, nexps, &e);
      if (nexps > nvars)
        ls->fs->freereg -= nexps - nvars;  /* remove extra values */
    }
    else {
      LUAK_setoneret(ls->fs, &e);  /* close last expression */
      LUAK_storevar(ls->fs, &lh->v, &e);
      return;  /* avoid default */
    }
  }
  init_exp(&e, VNONRELOC, ls->fs->freereg-1);  /* default assignment */
  LUAK_storevar(ls->fs, &lh->v, &e);
}


static int cond (LexState *ls) {
  /* cond -> exp */
  expdesc v;
  expr(ls, &v);  /* read condition */
  if (v.k == VNIL) v.k = VFALSE;  /* `falses' are all equal here */
  LUAK_goiftrue(ls->fs, &v);
  return v.f;
}


static void labelstat (LexState *ls, int pc) {
  int line = ls->linenumber;
  TString *comefrom;
  int g;
  if (testnext(ls, TK_DBCOLON)) {
    comefrom = str_checkname(ls);
    checknext(ls, TK_DBCOLON);
  } else {
    LUAX_next(ls);  /* skip break */
    comefrom = LUAS_new(ls->L, "BREAK");
  }
  g = newcomefromentry(ls, &ls->dyd->gt, comefrom, line, pc);
  findcomefrom(ls, g);  /* close it if comefrom already defined */
}


/* check for repeated comefroms on the same block */
static void checkrepeated (FuncState *fs, Labellist *ll, TString *comefrom) {
  int i;
  for (i = fs->bl->firstcomefrom; i < ll->n; i++) {
    if (LUAS_eqstr(comefrom, ll->arr[i].name)) {
      const char *msg = LUAO_pushfstring(fs->ls->L,
                          "comefrom " LUA_QS " already defined on line %d",
                          getstr(comefrom), ll->arr[i].line);
      semerror(fs->ls, msg);
    }
  }
}


/* skip no-op statements */
static void skipnoopstat (LexState *ls) {
  while (ls->t.token == ';' || ls->t.token == TK_COMEFROM)
    statement(ls);
}


static void comefromstat (LexState *ls, TString *comefrom, int line) {
  /* comefrom -> '::' NAME '::' */
  FuncState *fs = ls->fs;
  Labellist *ll = &ls->dyd->comefrom;
  int l;  /* index of new comefrom being created */
  checkrepeated(fs, ll, comefrom);  /* check for repeated comefroms */
  /* create new entry for this comefrom */
  l = newcomefromentry(ls, ll, comefrom, line, fs->pc);
  skipnoopstat(ls);  /* skip other no-op statements */
  if (block_follow(ls, 0)) {  /* comefrom is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    ll->arr[l].nactvar = fs->bl->nactvar;
  }
  findlabels(ls, &ll->arr[l]);
}


static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  LUAX_next(ls);  /* skip WHILE */
  whileinit = LUAK_getcomefrom(fs);
  condexit = cond(ls);
  enterblock(fs, &bl, 1);
  checknext(ls, TK_DO);
  block(ls);
  LUAK_jumpto(fs, whileinit);
  check_match(ls, TK_END, TK_WHILE, line);
  leaveblock(fs);
  LUAK_patchtohere(fs, condexit);  /* false conditions finish the loop */
}


static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  int repeat_init = LUAK_getcomefrom(fs);
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  LUAX_next(ls);  /* skip REPEAT */
  statlist(ls);
  check_match(ls, TK_UNTIL, TK_REPEAT, line);
  condexit = cond(ls);  /* read condition (inside scope block) */
  if (bl2.upval)  /* upvalues? */
    LUAK_patchclose(fs, condexit, bl2.nactvar);
  leaveblock(fs);  /* finish scope */
  LUAK_patchlist(fs, condexit, repeat_init);  /* close the loop */
  leaveblock(fs);  /* finish loop */
}


static int exp1 (LexState *ls) {
  expdesc e;
  int reg;
  expr(ls, &e);
  LUAK_exp2nextreg(ls->fs, &e);
  LUA_assert(e.k == VNONRELOC);
  reg = e.u.info;
  return reg;
}


static void forbody (LexState *ls, int base, int line, int nvars, int isnum) {
  /* forbody -> DO block */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  adjustlocalvars(ls, 3);  /* control variables */
  checknext(ls, TK_DO);
  prep = isnum ? LUAK_codeAsBx(fs, OP_FORPREP, base, NO_JUMP) : LUAK_jump(fs);
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(ls, nvars);
  LUAK_reserveregs(fs, nvars);
  block(ls);
  leaveblock(fs);  /* end of scope for declared variables */
  LUAK_patchtohere(fs, prep);
  if (isnum)  /* numeric for? */
    endfor = LUAK_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP);
  else {  /* generic for */
    LUAK_codeABC(fs, OP_TFORCALL, base, 0, nvars);
    LUAK_fixline(fs, line);
    endfor = LUAK_codeAsBx(fs, OP_TFORLOOP, base + 2, NO_JUMP);
  }
  LUAK_patchlist(fs, endfor, prep + 1);
  LUAK_fixline(fs, line);
}


static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp1,exp1[,exp1] forbody */
  FuncState *fs = ls->fs;
  int base = fs->freereg;
  new_localvarliteral(ls, "(for index)");
  new_localvarliteral(ls, "(for limit)");
  new_localvarliteral(ls, "(for step)");
  new_localvar(ls, varname);
  checknext(ls, '=');
  exp1(ls);  /* initial value */
  checknext(ls, ',');
  exp1(ls);  /* limit */
  if (testnext(ls, ','))
    exp1(ls);  /* optional step */
  else {  /* default step = 1 */
    LUAK_codek(fs, fs->freereg, LUAK_numberK(fs, 1));
    LUAK_reserveregs(fs, 1);
  }
  forbody(ls, base, line, 1, 1);
}


static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  int nvars = 4;  /* gen, state, control, plus at least one declared var */
  int line;
  int base = fs->freereg;
  /* create control variables */
  new_localvarliteral(ls, "(for generator)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for control)");
  /* create declared variables */
  new_localvar(ls, indexname);
  while (testnext(ls, ',')) {
    new_localvar(ls, str_checkname(ls));
    nvars++;
  }
  checknext(ls, TK_IN);
  line = ls->linenumber;
  adjust_assign(ls, 3, explist(ls, &e), &e);
  LUAK_checkstack(fs, 3);  /* extra space to call generator */
  forbody(ls, base, line, nvars - 3, 0);
}


static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->fs;
  TString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  LUAX_next(ls);  /* skip `for' */
  varname = str_checkname(ls);  /* first variable name */
  switch (ls->t.token) {
    case '=': fornum(ls, varname, line); break;
    case ',': case TK_IN: forlist(ls, varname); break;
    default: LUAX_syntaxerror(ls, LUA_QL("=") " or " LUA_QL("IN") " expected");
  }
  check_match(ls, TK_END, TK_FOR, line);
  leaveblock(fs);  /* loop scope (`break' jumps to this point) */
}


static void test_then_block (LexState *ls, int *escapelist) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  expdesc v;
  int jf;  /* instruction to skip 'then' code (if condition is false) */
  LUAX_next(ls);  /* skip IF or ELSEIF */
  expr(ls, &v);  /* read condition */
  checknext(ls, TK_THEN);
  if (ls->t.token == TK_DBCOLON || ls->t.token == TK_BREAK) {
    LUAK_goiffalse(ls->fs, &v);  /* will jump to comefrom if condition is true */
    enterblock(fs, &bl, 0);  /* must enter block before 'label' */
    labelstat(ls, v.t);  /* handle label/break */
    skipnoopstat(ls);  /* skip other no-op statements */
    if (block_follow(ls, 0)) {  /* 'label' is the entire block? */
      leaveblock(fs);
      return;  /* and that is it */
    }
    else  /* must skip over 'then' part if condition is false */
      jf = LUAK_jump(fs);
  }
  else {  /* regular case (not label/break) */
    LUAK_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
    enterblock(fs, &bl, 0);
    jf = v.f;
  }
  statlist(ls);  /* `then' part */
  leaveblock(fs);
  if (ls->t.token == TK_ELSE ||
      ls->t.token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    LUAK_concat(fs, escapelist, LUAK_jump(fs));  /* must jump over it */
  LUAK_patchtohere(fs, jf);
}


static void ifstat (LexState *ls, int line) {
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *fs = ls->fs;
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  test_then_block(ls, &escapelist);  /* IF cond THEN block */
  while (ls->t.token == TK_ELSEIF)
    test_then_block(ls, &escapelist);  /* ELSEIF cond THEN block */
  if (testnext(ls, TK_ELSE))
    block(ls);  /* `else' part */
  check_match(ls, TK_END, TK_IF, line);
  LUAK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


static void localfunc (LexState *ls) {
  expdesc b;
  FuncState *fs = ls->fs;
  new_localvar(ls, str_checkname(ls));  /* new local variable */
  adjustlocalvars(ls, 1);  /* enter its scope */
  body(ls, &b, 0, ls->linenumber);  /* function created in next register */
  /* debug information will only see the variable after this point! */
  getlocvar(fs, b.u.info)->startpc = fs->pc;
}


static void localstat (LexState *ls) {
  /* stat -> LOCAL NAME {`,' NAME} [`=' explist] */
  int nvars = 0;
  int nexps;
  expdesc e;
  do {
    new_localvar(ls, str_checkname(ls));
    nvars++;
  } while (testnext(ls, ','));
  if (testnext(ls, '='))
    nexps = explist(ls, &e);
  else {
    e.k = VVOID;
    nexps = 0;
  }
  adjust_assign(ls, nvars, nexps, &e);
  adjustlocalvars(ls, nvars);
}


static int funcname (LexState *ls, expdesc *v) {
  /* funcname -> NAME {fieldsel} [`:' NAME] */
  int ismethod = 0;
  singlevar(ls, v);
  while (ls->t.token == '.')
    fieldsel(ls, v);
  if (ls->t.token == ':') {
    ismethod = 1;
    fieldsel(ls, v);
  }
  return ismethod;
}


static void funcstat (LexState *ls, int line) {
  /* funcstat -> FUNCTION funcname body */
  int ismethod;
  expdesc v, b;
  LUAX_next(ls);  /* skip FUNCTION */
  ismethod = funcname(ls, &v);
  body(ls, &b, ismethod, line);
  LUAK_storevar(ls->fs, &v, &b);
  LUAK_fixline(ls->fs, line);  /* definition `happens' in the first line */
}


static void exprstat (LexState *ls) {
  /* stat -> func | assignment */
  FuncState *fs = ls->fs;
  struct LHS_assign v;
  suffixedexp(ls, &v.v);
  if (ls->t.token == '=' || ls->t.token == ',') { /* stat -> assignment ? */
    v.prev = NULL;
    assignment(ls, &v, 1);
  }
  else {  /* stat -> func */
    check_condition(ls, v.v.k == VCALL, "syntax error");
    SETARG_C(getcode(fs, &v.v), 1);  /* call statement uses no results */
  }
}


static void retstat (LexState *ls) {
  /* stat -> RETURN [explist] [';'] */
  FuncState *fs = ls->fs;
  expdesc e;
  int first, nret;  /* registers with returned values */
  if (block_follow(ls, 1) || ls->t.token == ';')
    first = nret = 0;  /* return no values */
  else {
    nret = explist(ls, &e);  /* optional return values */
    if (hasmultret(e.k)) {
      LUAK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1) {  /* tail call? */
        SET_OPCODE(getcode(fs,&e), OP_TAILCALL);
        LUA_assert(GETARG_A(getcode(fs,&e)) == fs->nactvar);
      }
      first = fs->nactvar;
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1)  /* only one single value? */
        first = LUAK_exp2anyreg(fs, &e);
      else {
        LUAK_exp2nextreg(fs, &e);  /* values must go to the `stack' */
        first = fs->nactvar;  /* return all `active' values */
        LUA_assert(nret == fs->freereg - first);
      }
    }
  }
  LUAK_ret(fs, first, nret);
  testnext(ls, ';');  /* skip optional semicolon */
}


static void statement (LexState *ls) {
  int line = ls->linenumber;  /* may be needed for error messages */
  enterlevel(ls);
  switch (ls->t.token) {
    case ';': {  /* stat -> ';' (empty statement) */
      LUAX_next(ls);  /* skip ';' */
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      ifstat(ls, line);
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(ls, line);
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      LUAX_next(ls);  /* skip DO */
      block(ls);
      check_match(ls, TK_END, TK_DO, line);
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(ls, line);
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(ls, line);
      break;
    }
    case TK_FUNCTION: {  /* stat -> funcstat */
      funcstat(ls, line);
      break;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      LUAX_next(ls);  /* skip LOCAL */
      if (testnext(ls, TK_FUNCTION))  /* local function? */
        localfunc(ls);
      else
        localstat(ls);
      break;
    }
    case TK_COMEFROM: {  /* stat -> comefrom */
      LUAX_next(ls);  /* skip COMEFROM */
      comefromstat(ls, str_checkname(ls), line);
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      LUAX_next(ls);  /* skip RETURN */
      retstat(ls);
      break;
    }
    case TK_BREAK:   /* stat -> breakstat */
    case TK_DBCOLON: {  /* stat -> 'label' NAME */
      labelstat(ls, LUAK_jump(ls->fs));
      break;
    }
    default: {  /* stat -> func | assignment */
      exprstat(ls);
      break;
    }
  }
  LUA_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
             ls->fs->freereg >= ls->fs->nactvar);
  ls->fs->freereg = ls->fs->nactvar;  /* free registers */
  leavelevel(ls);
}

/* }====================================================================== */


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
static void mainfunc (LexState *ls, FuncState *fs) {
  BlockCnt bl;
  expdesc v;
  open_func(ls, fs, &bl);
  fs->f->is_vararg = 1;  /* main function is always vararg */
  init_exp(&v, VLOCAL, 0);  /* create and... */
  newupvalue(fs, ls->envn, &v);  /* ...set environment upvalue */
  LUAX_next(ls);  /* read first token */
  statlist(ls);  /* parse main body */
  check(ls, TK_EOS);
  close_func(ls);
}


Closure *LUAY_parser (LUA_State *L, ZIO *z, Mbuffer *buff,
                      Dyndata *dyd, const char *name, int firstchar) {
  LexState lexstate;
  FuncState funcstate;
  Closure *cl = LUAF_newLclosure(L, 1);  /* create main closure */
  /* anchor closure (to avoid being collected) */
  setclLvalue(L, L->top, cl);
  incr_top(L);
  funcstate.f = cl->l.p = LUAF_newproto(L);
  funcstate.f->source = LUAS_new(L, name);  /* create and anchor TString */
  lexstate.buff = buff;
  lexstate.dyd = dyd;
  dyd->actvar.n = dyd->gt.n = dyd->comefrom.n = 0;
  LUAX_setinput(L, &lexstate, z, funcstate.f->source, firstchar);
  mainfunc(&lexstate, &funcstate);
  LUA_assert(!funcstate.prev && funcstate.nups == 1 && !lexstate.fs);
  /* all scopes should be correctly finished */
  LUA_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->comefrom.n == 0);
  return cl;  /* it's on the stack too */
}

