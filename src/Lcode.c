/*
** $Id: lcode.c,v 2.62 2012/08/16 17:34:28 roberto Exp $
** Code generator for LUA
** See Copyright Notice in LUA.h
*/


#include <stdlib.h>

#define lcode_c
#define LUA_CORE

#include "LUA.h"

#include "Lcode.h"
#include "Ldebug.h"
#include "Ldo.h"
#include "Lgc.h"
#include "Llex.h"
#include "Lmem.h"
#include "Lobject.h"
#include "Lopcodes.h"
#include "Lparser.h"
#include "Lstring.h"
#include "Ltable.h"
#include "Lvm.h"


#define hasjumps(e)	((e)->t != (e)->f)


static int isnumeral(expdesc *e) {
  return (e->k == VKNUM && e->t == NO_JUMP && e->f == NO_JUMP);
}


void LUAK_nil (FuncState *fs, int from, int n) {
  Instruction *previous;
  int l = from + n - 1;  /* last register to set nil */
  if (fs->pc > fs->lasttarget) {  /* no jumps to current position? */
    previous = &fs->f->code[fs->pc-1];
    if (GET_OPCODE(*previous) == OP_LOADNIL) {
      int pfrom = GETARG_A(*previous);
      int pl = pfrom + GETARG_B(*previous);
      if ((pfrom <= from && from <= pl + 1) ||
          (from <= pfrom && pfrom <= l + 1)) {  /* can connect both? */
        if (pfrom < from) from = pfrom;  /* from = min(from, pfrom) */
        if (pl > l) l = pl;  /* l = max(l, pl) */
        SETARG_A(*previous, from);
        SETARG_B(*previous, l - from);
        return;
      }
    }  /* else go through */
  }
  LUAK_codeABC(fs, OP_LOADNIL, from, n - 1, 0);  /* else no optimization */
}


int LUAK_jump (FuncState *fs) {
  int jpc = fs->jpc;  /* save list of jumps to here */
  int j;
  fs->jpc = NO_JUMP;
  j = LUAK_codeAsBx(fs, OP_JMP, 0, NO_JUMP);
  LUAK_concat(fs, &j, jpc);  /* keep them on hold */
  return j;
}


void LUAK_ret (FuncState *fs, int first, int nret) {
  LUAK_codeABC(fs, OP_RETURN, first, nret+1, 0);
}


static int condjump (FuncState *fs, OpCode op, int A, int B, int C) {
  LUAK_codeABC(fs, op, A, B, C);
  return LUAK_jump(fs);
}


static void fixjump (FuncState *fs, int pc, int dest) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest-(pc+1);
  LUA_assert(dest != NO_JUMP);
  if (abs(offset) > MAXARG_sBx)
    LUAX_syntaxerror(fs->ls, "control structure too long");
  SETARG_sBx(*jmp, offset);
}


/*
** returns current `pc' and marks it as a jump target (to avoid wrong
** optimizations with consecutive instructions not in the same basic block).
*/
int LUAK_getcomefrom (FuncState *fs) {
  fs->lasttarget = fs->pc;
  return fs->pc;
}


static int getjump (FuncState *fs, int pc) {
  int offset = GETARG_sBx(fs->f->code[pc]);
  if (offset == NO_JUMP)  /* point to itself represents end of list */
    return NO_JUMP;  /* end of list */
  else
    return (pc+1)+offset;  /* turn offset into absolute position */
}


static Instruction *getjumpcontrol (FuncState *fs, int pc) {
  Instruction *pi = &fs->f->code[pc];
  if (pc >= 1 && testTMode(GET_OPCODE(*(pi-1))))
    return pi-1;
  else
    return pi;
}


/*
** check whether list has any jump that do not produce a value
** (or produce an inverted value)
*/
static int need_value (FuncState *fs, int list) {
  for (; list != NO_JUMP; list = getjump(fs, list)) {
    Instruction i = *getjumpcontrol(fs, list);
    if (GET_OPCODE(i) != OP_TESTSET) return 1;
  }
  return 0;  /* not found */
}


static int patchtestreg (FuncState *fs, int node, int reg) {
  Instruction *i = getjumpcontrol(fs, node);
  if (GET_OPCODE(*i) != OP_TESTSET)
    return 0;  /* cannot patch other instructions */
  if (reg != NO_REG && reg != GETARG_B(*i))
    SETARG_A(*i, reg);
  else  /* no register to put value or register already has the value */
    *i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));

  return 1;
}


static void removevalues (FuncState *fs, int list) {
  for (; list != NO_JUMP; list = getjump(fs, list))
      patchtestreg(fs, list, NO_REG);
}


static void patchlistaux (FuncState *fs, int list, int vtarget, int reg,
                          int dtarget) {
  while (list != NO_JUMP) {
    int next = getjump(fs, list);
    if (patchtestreg(fs, list, reg))
      fixjump(fs, list, vtarget);
    else
      fixjump(fs, list, dtarget);  /* jump to default target */
    list = next;
  }
}


static void dischargejpc (FuncState *fs) {
  patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);
  fs->jpc = NO_JUMP;
}


void LUAK_patchlist (FuncState *fs, int list, int target) {
  if (target == fs->pc)
    LUAK_patchtohere(fs, list);
  else {
    LUA_assert(target < fs->pc);
    patchlistaux(fs, list, target, NO_REG, target);
  }
}


LUAI_FUNC void LUAK_patchclose (FuncState *fs, int list, int level) {
  level++;  /* argument is +1 to reserve 0 as non-op */
  while (list != NO_JUMP) {
    int next = getjump(fs, list);
    LUA_assert(GET_OPCODE(fs->f->code[list]) == OP_JMP &&
                (GETARG_A(fs->f->code[list]) == 0 ||
                 GETARG_A(fs->f->code[list]) >= level));
    SETARG_A(fs->f->code[list], level);
    list = next;
  }
}


void LUAK_patchtohere (FuncState *fs, int list) {
  LUAK_getcomefrom(fs);
  LUAK_concat(fs, &fs->jpc, list);
}


void LUAK_concat (FuncState *fs, int *l1, int l2) {
  if (l2 == NO_JUMP) return;
  else if (*l1 == NO_JUMP)
    *l1 = l2;
  else {
    int list = *l1;
    int next;
    while ((next = getjump(fs, list)) != NO_JUMP)  /* find last element */
      list = next;
    fixjump(fs, list, l2);
  }
}


static int LUAK_code (FuncState *fs, Instruction i) {
  Proto *f = fs->f;
  dischargejpc(fs);  /* `pc' will change */
  /* put new instruction in code array */
  LUAM_growvector(fs->ls->L, f->code, fs->pc, f->sizecode, Instruction,
                  MAX_INT, "opcodes");
  f->code[fs->pc] = i;
  /* save corresponding line information */
  LUAM_growvector(fs->ls->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
                  MAX_INT, "opcodes");
  f->lineinfo[fs->pc] = fs->ls->lastline;
  return fs->pc++;
}


int LUAK_codeABC (FuncState *fs, OpCode o, int a, int b, int c) {
  LUA_assert(getOpMode(o) == iABC);
  LUA_assert(getBMode(o) != OpArgN || b == 0);
  LUA_assert(getCMode(o) != OpArgN || c == 0);
  LUA_assert(a <= MAXARG_A && b <= MAXARG_B && c <= MAXARG_C);
  return LUAK_code(fs, CREATE_ABC(o, a, b, c));
}


int LUAK_codeABx (FuncState *fs, OpCode o, int a, unsigned int bc) {
  LUA_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
  LUA_assert(getCMode(o) == OpArgN);
  LUA_assert(a <= MAXARG_A && bc <= MAXARG_Bx);
  return LUAK_code(fs, CREATE_ABx(o, a, bc));
}


static int codeextraarg (FuncState *fs, int a) {
  LUA_assert(a <= MAXARG_Ax);
  return LUAK_code(fs, CREATE_Ax(OP_EXTRAARG, a));
}


int LUAK_codek (FuncState *fs, int reg, int k) {
  if (k <= MAXARG_Bx)
    return LUAK_codeABx(fs, OP_LOADK, reg, k);
  else {
    int p = LUAK_codeABx(fs, OP_LOADKX, reg, 0);
    codeextraarg(fs, k);
    return p;
  }
}


void LUAK_checkstack (FuncState *fs, int n) {
  int newstack = fs->freereg + n;
  if (newstack > fs->f->maxstacksize) {
    if (newstack >= MAXSTACK)
      LUAX_syntaxerror(fs->ls, "function or expression too complex");
    fs->f->maxstacksize = cast_byte(newstack);
  }
}


void LUAK_reserveregs (FuncState *fs, int n) {
  LUAK_checkstack(fs, n);
  fs->freereg += n;
}


static void freereg (FuncState *fs, int reg) {
  if (!ISK(reg) && reg >= fs->nactvar) {
    fs->freereg--;
    LUA_assert(reg == fs->freereg);
  }
}


static void freeexp (FuncState *fs, expdesc *e) {
  if (e->k == VNONRELOC)
    freereg(fs, e->u.info);
}


static int addk (FuncState *fs, TValue *key, TValue *v) {
  LUA_State *L = fs->ls->L;
  TValue *idx = LUAH_set(L, fs->h, key);
  Proto *f = fs->f;
  int k, oldsize;
  if (ttisnumber(idx)) {
    LUA_Number n = nvalue(idx);
    LUA_number2int(k, n);
    if (LUAV_rawequalobj(&f->k[k], v))
      return k;
    /* else may be a collision (e.g., between 0.0 and "\0\0\0\0\0\0\0\0");
       go through and create a new entry for this value */
  }
  /* constant not found; create a new entry */
  oldsize = f->sizek;
  k = fs->nk;
  /* numerical value does not need GC barrier;
     table has no metatable, so it does not need to invalidate cache */
  setnvalue(idx, cast_num(k));
  LUAM_growvector(L, f->k, k, f->sizek, TValue, MAXARG_Ax, "constants");
  while (oldsize < f->sizek) setnilvalue(&f->k[oldsize++]);
  setobj(L, &f->k[k], v);
  fs->nk++;
  LUAC_barrier(L, f, v);
  return k;
}


int LUAK_stringK (FuncState *fs, TString *s) {
  TValue o;
  setsvalue(fs->ls->L, &o, s);
  return addk(fs, &o, &o);
}


int LUAK_numberK (FuncState *fs, LUA_Number r) {
  int n;
  LUA_State *L = fs->ls->L;
  TValue o;
  setnvalue(&o, r);
  if (r == 0 || LUAi_numisnan(NULL, r)) {  /* handle -0 and NaN */
    /* use raw representation as key to avoid numeric problems */
    setsvalue(L, L->top++, LUAS_newlstr(L, (char *)&r, sizeof(r)));
    n = addk(fs, L->top - 1, &o);
    L->top--;
  }
  else
    n = addk(fs, &o, &o);  /* regular case */
  return n;
}


static int boolK (FuncState *fs, int b) {
  TValue o;
  setbvalue(&o, b);
  return addk(fs, &o, &o);
}


static int nilK (FuncState *fs) {
  TValue k, v;
  setnilvalue(&v);
  /* cannot use nil as key; instead use table itself to represent nil */
  sethvalue(fs->ls->L, &k, fs->h);
  return addk(fs, &k, &v);
}


void LUAK_setreturns (FuncState *fs, expdesc *e, int nresults) {
  if (e->k == VCALL) {  /* expression is an open function call? */
    SETARG_C(getcode(fs, e), nresults+1);
  }
  else if (e->k == VVARARG) {
    SETARG_B(getcode(fs, e), nresults+1);
    SETARG_A(getcode(fs, e), fs->freereg);
    LUAK_reserveregs(fs, 1);
  }
}


void LUAK_setoneret (FuncState *fs, expdesc *e) {
  if (e->k == VCALL) {  /* expression is an open function call? */
    e->k = VNONRELOC;
    e->u.info = GETARG_A(getcode(fs, e));
  }
  else if (e->k == VVARARG) {
    SETARG_B(getcode(fs, e), 2);
    e->k = VRELOCABLE;  /* can relocate its simple result */
  }
}


void LUAK_dischargevars (FuncState *fs, expdesc *e) {
  switch (e->k) {
    case VLOCAL: {
      e->k = VNONRELOC;
      break;
    }
    case VUPVAL: {
      e->u.info = LUAK_codeABC(fs, OP_GETUPVAL, 0, e->u.info, 0);
      e->k = VRELOCABLE;
      break;
    }
    case VINDEXED: {
      OpCode op = OP_GETTABUP;  /* assume 't' is in an upvalue */
      freereg(fs, e->u.ind.idx);
      if (e->u.ind.vt == VLOCAL) {  /* 't' is in a register? */
        freereg(fs, e->u.ind.t);
        op = OP_GETTABLE;
      }
      e->u.info = LUAK_codeABC(fs, op, 0, e->u.ind.t, e->u.ind.idx);
      e->k = VRELOCABLE;
      break;
    }
    case VVARARG:
    case VCALL: {
      LUAK_setoneret(fs, e);
      break;
    }
    default: break;  /* there is one value available (somewhere) */
  }
}


static int code_comefrom (FuncState *fs, int A, int b, int jump) {
  LUAK_getcomefrom(fs);  /* those instructions may be jump targets */
  return LUAK_codeABC(fs, OP_LOADBOOL, A, b, jump);
}


static void discharge2reg (FuncState *fs, expdesc *e, int reg) {
  LUAK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: {
      LUAK_nil(fs, reg, 1);
      break;
    }
    case VFALSE: case VTRUE: {
      LUAK_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
      break;
    }
    case VK: {
      LUAK_codek(fs, reg, e->u.info);
      break;
    }
    case VKNUM: {
      LUAK_codek(fs, reg, LUAK_numberK(fs, e->u.nval));
      break;
    }
    case VRELOCABLE: {
      Instruction *pc = &getcode(fs, e);
      SETARG_A(*pc, reg);
      break;
    }
    case VNONRELOC: {
      if (reg != e->u.info)
        LUAK_codeABC(fs, OP_MOVE, reg, e->u.info, 0);
      break;
    }
    default: {
      LUA_assert(e->k == VVOID || e->k == VJMP);
      return;  /* nothing to do... */
    }
  }
  e->u.info = reg;
  e->k = VNONRELOC;
}


static void discharge2anyreg (FuncState *fs, expdesc *e) {
  if (e->k != VNONRELOC) {
    LUAK_reserveregs(fs, 1);
    discharge2reg(fs, e, fs->freereg-1);
  }
}


static void exp2reg (FuncState *fs, expdesc *e, int reg) {
  discharge2reg(fs, e, reg);
  if (e->k == VJMP)
    LUAK_concat(fs, &e->t, e->u.info);  /* put this jump in `t' list */
  if (hasjumps(e)) {
    int final;  /* position after whole expression */
    int p_f = NO_JUMP;  /* position of an eventual LOAD false */
    int p_t = NO_JUMP;  /* position of an eventual LOAD true */
    if (need_value(fs, e->t) || need_value(fs, e->f)) {
      int fj = (e->k == VJMP) ? NO_JUMP : LUAK_jump(fs);
      p_f = code_comefrom(fs, reg, 0, 1);
      p_t = code_comefrom(fs, reg, 1, 0);
      LUAK_patchtohere(fs, fj);
    }
    final = LUAK_getcomefrom(fs);
    patchlistaux(fs, e->f, final, reg, p_f);
    patchlistaux(fs, e->t, final, reg, p_t);
  }
  e->f = e->t = NO_JUMP;
  e->u.info = reg;
  e->k = VNONRELOC;
}


void LUAK_exp2nextreg (FuncState *fs, expdesc *e) {
  LUAK_dischargevars(fs, e);
  freeexp(fs, e);
  LUAK_reserveregs(fs, 1);
  exp2reg(fs, e, fs->freereg - 1);
}


int LUAK_exp2anyreg (FuncState *fs, expdesc *e) {
  LUAK_dischargevars(fs, e);
  if (e->k == VNONRELOC) {
    if (!hasjumps(e)) return e->u.info;  /* exp is already in a register */
    if (e->u.info >= fs->nactvar) {  /* reg. is not a local? */
      exp2reg(fs, e, e->u.info);  /* put value on it */
      return e->u.info;
    }
  }
  LUAK_exp2nextreg(fs, e);  /* default */
  return e->u.info;
}


void LUAK_exp2anyregup (FuncState *fs, expdesc *e) {
  if (e->k != VUPVAL || hasjumps(e))
    LUAK_exp2anyreg(fs, e);
}


void LUAK_exp2val (FuncState *fs, expdesc *e) {
  if (hasjumps(e))
    LUAK_exp2anyreg(fs, e);
  else
    LUAK_dischargevars(fs, e);
}


int LUAK_exp2RK (FuncState *fs, expdesc *e) {
  LUAK_exp2val(fs, e);
  switch (e->k) {
    case VTRUE:
    case VFALSE:
    case VNIL: {
      if (fs->nk <= MAXINDEXRK) {  /* constant fits in RK operand? */
        e->u.info = (e->k == VNIL) ? nilK(fs) : boolK(fs, (e->k == VTRUE));
        e->k = VK;
        return RKASK(e->u.info);
      }
      else break;
    }
    case VKNUM: {
      e->u.info = LUAK_numberK(fs, e->u.nval);
      e->k = VK;
      /* go through */
    }
    case VK: {
      if (e->u.info <= MAXINDEXRK)  /* constant fits in argC? */
        return RKASK(e->u.info);
      else break;
    }
    default: break;
  }
  /* not a constant in the right range: put it in a register */
  return LUAK_exp2anyreg(fs, e);
}


void LUAK_storevar (FuncState *fs, expdesc *var, expdesc *ex) {
  switch (var->k) {
    case VLOCAL: {
      freeexp(fs, ex);
      exp2reg(fs, ex, var->u.info);
      return;
    }
    case VUPVAL: {
      int e = LUAK_exp2anyreg(fs, ex);
      LUAK_codeABC(fs, OP_SETUPVAL, e, var->u.info, 0);
      break;
    }
    case VINDEXED: {
      OpCode op = (var->u.ind.vt == VLOCAL) ? OP_SETTABLE : OP_SETTABUP;
      int e = LUAK_exp2RK(fs, ex);
      LUAK_codeABC(fs, op, var->u.ind.t, var->u.ind.idx, e);
      break;
    }
    default: {
      LUA_assert(0);  /* invalid var kind to store */
      break;
    }
  }
  freeexp(fs, ex);
}


void LUAK_self (FuncState *fs, expdesc *e, expdesc *key) {
  int ereg;
  LUAK_exp2anyreg(fs, e);
  ereg = e->u.info;  /* register where 'e' was placed */
  freeexp(fs, e);
  e->u.info = fs->freereg;  /* base register for op_self */
  e->k = VNONRELOC;
  LUAK_reserveregs(fs, 2);  /* function and 'self' produced by op_self */
  LUAK_codeABC(fs, OP_SELF, e->u.info, ereg, LUAK_exp2RK(fs, key));
  freeexp(fs, key);
}


static void invertjump (FuncState *fs, expdesc *e) {
  Instruction *pc = getjumpcontrol(fs, e->u.info);
  LUA_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
                                           GET_OPCODE(*pc) != OP_TEST);
  SETARG_A(*pc, !(GETARG_A(*pc)));
}


static int jumponcond (FuncState *fs, expdesc *e, int cond) {
  if (e->k == VRELOCABLE) {
    Instruction ie = getcode(fs, e);
    if (GET_OPCODE(ie) == OP_NOT) {
      fs->pc--;  /* remove previous OP_NOT */
      return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
    }
    /* else go through */
  }
  discharge2anyreg(fs, e);
  freeexp(fs, e);
  return condjump(fs, OP_TESTSET, NO_REG, e->u.info, cond);
}


void LUAK_goiftrue (FuncState *fs, expdesc *e) {
  int pc;  /* pc of last jump */
  LUAK_dischargevars(fs, e);
  switch (e->k) {
    case VJMP: {
      invertjump(fs, e);
      pc = e->u.info;
      break;
    }
    case VK: case VKNUM: case VTRUE: {
      pc = NO_JUMP;  /* always true; do nothing */
      break;
    }
    default: {
      pc = jumponcond(fs, e, 0);
      break;
    }
  }
  LUAK_concat(fs, &e->f, pc);  /* insert last jump in `f' list */
  LUAK_patchtohere(fs, e->t);
  e->t = NO_JUMP;
}


void LUAK_goiffalse (FuncState *fs, expdesc *e) {
  int pc;  /* pc of last jump */
  LUAK_dischargevars(fs, e);
  switch (e->k) {
    case VJMP: {
      pc = e->u.info;
      break;
    }
    case VNIL: case VFALSE: {
      pc = NO_JUMP;  /* always false; do nothing */
      break;
    }
    default: {
      pc = jumponcond(fs, e, 1);
      break;
    }
  }
  LUAK_concat(fs, &e->t, pc);  /* insert last jump in `t' list */
  LUAK_patchtohere(fs, e->f);
  e->f = NO_JUMP;
}


static void codenot (FuncState *fs, expdesc *e) {
  LUAK_dischargevars(fs, e);
  switch (e->k) {
    case VNIL: case VFALSE: {
      e->k = VTRUE;
      break;
    }
    case VK: case VKNUM: case VTRUE: {
      e->k = VFALSE;
      break;
    }
    case VJMP: {
      invertjump(fs, e);
      break;
    }
    case VRELOCABLE:
    case VNONRELOC: {
      discharge2anyreg(fs, e);
      freeexp(fs, e);
      e->u.info = LUAK_codeABC(fs, OP_NOT, 0, e->u.info, 0);
      e->k = VRELOCABLE;
      break;
    }
    default: {
      LUA_assert(0);  /* cannot happen */
      break;
    }
  }
  /* interchange true and false lists */
  { int temp = e->f; e->f = e->t; e->t = temp; }
  removevalues(fs, e->f);
  removevalues(fs, e->t);
}


void LUAK_indexed (FuncState *fs, expdesc *t, expdesc *k) {
  LUA_assert(!hasjumps(t));
  t->u.ind.t = t->u.info;
  t->u.ind.idx = LUAK_exp2RK(fs, k);
  t->u.ind.vt = (t->k == VUPVAL) ? VUPVAL
                                 : check_exp(vkisinreg(t->k), VLOCAL);
  t->k = VINDEXED;
}


static int constfolding (OpCode op, expdesc *e1, expdesc *e2) {
  LUA_Number r;
  if (!isnumeral(e1) || !isnumeral(e2)) return 0;
  if ((op == OP_DIV || op == OP_MOD) && e2->u.nval == 0)
    return 0;  /* do not attempt to divide by 0 */
  r = LUAO_arith(op - OP_ADD + LUA_OPADD, e1->u.nval, e2->u.nval);
  e1->u.nval = r;
  return 1;
}


static void codearith (FuncState *fs, OpCode op,
                       expdesc *e1, expdesc *e2, int line) {
  if (constfolding(op, e1, e2))
    return;
  else {
    int o2 = (op != OP_UNM && op != OP_LEN) ? LUAK_exp2RK(fs, e2) : 0;
    int o1 = LUAK_exp2RK(fs, e1);
    if (o1 > o2) {
      freeexp(fs, e1);
      freeexp(fs, e2);
    }
    else {
      freeexp(fs, e2);
      freeexp(fs, e1);
    }
    e1->u.info = LUAK_codeABC(fs, op, 0, o1, o2);
    e1->k = VRELOCABLE;
    LUAK_fixline(fs, line);
  }
}


static void codecomp (FuncState *fs, OpCode op, int cond, expdesc *e1,
                                                          expdesc *e2) {
  int o1 = LUAK_exp2RK(fs, e1);
  int o2 = LUAK_exp2RK(fs, e2);
  freeexp(fs, e2);
  freeexp(fs, e1);
  if (cond == 0 && op != OP_EQ) {
    int temp;  /* exchange args to replace by `<' or `<=' */
    temp = o1; o1 = o2; o2 = temp;  /* o1 <==> o2 */
    cond = 1;
  }
  e1->u.info = condjump(fs, op, cond, o1, o2);
  e1->k = VJMP;
}


void LUAK_prefix (FuncState *fs, UnOpr op, expdesc *e, int line) {
  expdesc e2;
  e2.t = e2.f = NO_JUMP; e2.k = VKNUM; e2.u.nval = 0;
  switch (op) {
    case OPR_MINUS: {
      if (isnumeral(e))  /* minus constant? */
        e->u.nval = LUAi_numunm(NULL, e->u.nval);  /* fold it */
      else {
        LUAK_exp2anyreg(fs, e);
        codearith(fs, OP_UNM, e, &e2, line);
      }
      break;
    }
    case OPR_NOT: codenot(fs, e); break;
    case OPR_LEN: {
      LUAK_exp2anyreg(fs, e);  /* cannot operate on constants */
      codearith(fs, OP_LEN, e, &e2, line);
      break;
    }
    default: LUA_assert(0);
  }
}


void LUAK_infix (FuncState *fs, BinOpr op, expdesc *v) {
  switch (op) {
    case OPR_AND: {
      LUAK_goiftrue(fs, v);
      break;
    }
    case OPR_OR: {
      LUAK_goiffalse(fs, v);
      break;
    }
    case OPR_CONCAT: {
      LUAK_exp2nextreg(fs, v);  /* operand must be on the `stack' */
      break;
    }
    case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
    case OPR_MOD: case OPR_POW: {
      if (!isnumeral(v)) LUAK_exp2RK(fs, v);
      break;
    }
    default: {
      LUAK_exp2RK(fs, v);
      break;
    }
  }
}


void LUAK_posfix (FuncState *fs, BinOpr op,
                  expdesc *e1, expdesc *e2, int line) {
  switch (op) {
    case OPR_AND: {
      LUA_assert(e1->t == NO_JUMP);  /* list must be closed */
      LUAK_dischargevars(fs, e2);
      LUAK_concat(fs, &e2->f, e1->f);
      *e1 = *e2;
      break;
    }
    case OPR_OR: {
      LUA_assert(e1->f == NO_JUMP);  /* list must be closed */
      LUAK_dischargevars(fs, e2);
      LUAK_concat(fs, &e2->t, e1->t);
      *e1 = *e2;
      break;
    }
    case OPR_CONCAT: {
      LUAK_exp2val(fs, e2);
      if (e2->k == VRELOCABLE && GET_OPCODE(getcode(fs, e2)) == OP_CONCAT) {
        LUA_assert(e1->u.info == GETARG_B(getcode(fs, e2))-1);
        freeexp(fs, e1);
        SETARG_B(getcode(fs, e2), e1->u.info);
        e1->k = VRELOCABLE; e1->u.info = e2->u.info;
      }
      else {
        LUAK_exp2nextreg(fs, e2);  /* operand must be on the 'stack' */
        codearith(fs, OP_CONCAT, e1, e2, line);
      }
      break;
    }
    case OPR_ADD: case OPR_SUB: case OPR_MUL: case OPR_DIV:
    case OPR_MOD: case OPR_POW: {
      codearith(fs, cast(OpCode, op - OPR_ADD + OP_ADD), e1, e2, line);
      break;
    }
    case OPR_EQ: case OPR_LT: case OPR_LE: {
      codecomp(fs, cast(OpCode, op - OPR_EQ + OP_EQ), 1, e1, e2);
      break;
    }
    case OPR_NE: case OPR_GT: case OPR_GE: {
      codecomp(fs, cast(OpCode, op - OPR_NE + OP_EQ), 0, e1, e2);
      break;
    }
    default: LUA_assert(0);
  }
}


void LUAK_fixline (FuncState *fs, int line) {
  fs->f->lineinfo[fs->pc - 1] = line;
}


void LUAK_setlist (FuncState *fs, int base, int nelems, int tostore) {
  int c =  (nelems - 1)/LFIELDS_PER_FLUSH + 1;
  int b = (tostore == LUA_MULTRET) ? 0 : tostore;
  LUA_assert(tostore != 0);
  if (c <= MAXARG_C)
    LUAK_codeABC(fs, OP_SETLIST, base, b, c);
  else if (c <= MAXARG_Ax) {
    LUAK_codeABC(fs, OP_SETLIST, base, b, 0);
    codeextraarg(fs, c);
  }
  else
    LUAX_syntaxerror(fs->ls, "constructor too long");
  fs->freereg = base + 1;  /* free registers with list values */
}

