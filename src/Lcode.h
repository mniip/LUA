/*
** $Id: Lcode.h,v 1.58 2011/08/30 16:26:41 roberto Exp $
** Code generator for LUA
** See Copyright Notice in LUA.h
*/

#ifndef lcode_h
#define lcode_h

#include "Llex.h"
#include "Lobject.h"
#include "Lopcodes.h"
#include "Lparser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
#define NO_JUMP (-1)


/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
typedef enum BinOpr {
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV, OPR_MOD, OPR_POW,
  OPR_CONCAT,
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;


typedef enum UnOpr { OPR_MINUS, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


#define getcode(fs,e)	((fs)->f->code[(e)->u.info])

#define LUAK_codeAsBx(fs,o,A,sBx)	LUAK_codeABx(fs,o,A,(sBx)+MAXARG_sBx)

#define LUAK_setmultret(fs,e)	LUAK_setreturns(fs, e, LUA_MULTRET)

#define LUAK_jumpto(fs,t)	LUAK_patchlist(fs, LUAK_jump(fs), t)

LUAI_FUNC int LUAK_codeABx (FuncState *fs, OpCode o, int A, unsigned int Bx);
LUAI_FUNC int LUAK_codeABC (FuncState *fs, OpCode o, int A, int B, int C);
LUAI_FUNC int LUAK_codek (FuncState *fs, int reg, int k);
LUAI_FUNC void LUAK_fixline (FuncState *fs, int line);
LUAI_FUNC void LUAK_nil (FuncState *fs, int from, int n);
LUAI_FUNC void LUAK_reserveregs (FuncState *fs, int n);
LUAI_FUNC void LUAK_checkstack (FuncState *fs, int n);
LUAI_FUNC int LUAK_stringK (FuncState *fs, TString *s);
LUAI_FUNC int LUAK_numberK (FuncState *fs, LUA_Number r);
LUAI_FUNC void LUAK_dischargevars (FuncState *fs, expdesc *e);
LUAI_FUNC int LUAK_exp2anyreg (FuncState *fs, expdesc *e);
LUAI_FUNC void LUAK_exp2anyregup (FuncState *fs, expdesc *e);
LUAI_FUNC void LUAK_exp2nextreg (FuncState *fs, expdesc *e);
LUAI_FUNC void LUAK_exp2val (FuncState *fs, expdesc *e);
LUAI_FUNC int LUAK_exp2RK (FuncState *fs, expdesc *e);
LUAI_FUNC void LUAK_self (FuncState *fs, expdesc *e, expdesc *key);
LUAI_FUNC void LUAK_indexed (FuncState *fs, expdesc *t, expdesc *k);
LUAI_FUNC void LUAK_goiftrue (FuncState *fs, expdesc *e);
LUAI_FUNC void LUAK_goiffalse (FuncState *fs, expdesc *e);
LUAI_FUNC void LUAK_storevar (FuncState *fs, expdesc *var, expdesc *e);
LUAI_FUNC void LUAK_setreturns (FuncState *fs, expdesc *e, int nresults);
LUAI_FUNC void LUAK_setoneret (FuncState *fs, expdesc *e);
LUAI_FUNC int LUAK_jump (FuncState *fs);
LUAI_FUNC void LUAK_ret (FuncState *fs, int first, int nret);
LUAI_FUNC void LUAK_patchlist (FuncState *fs, int list, int target);
LUAI_FUNC void LUAK_patchtohere (FuncState *fs, int list);
LUAI_FUNC void LUAK_patchclose (FuncState *fs, int list, int level);
LUAI_FUNC void LUAK_concat (FuncState *fs, int *l1, int l2);
LUAI_FUNC int LUAK_getcomefrom (FuncState *fs);
LUAI_FUNC void LUAK_prefix (FuncState *fs, UnOpr op, expdesc *v, int line);
LUAI_FUNC void LUAK_infix (FuncState *fs, BinOpr op, expdesc *v);
LUAI_FUNC void LUAK_posfix (FuncState *fs, BinOpr op, expdesc *v1,
                            expdesc *v2, int line);
LUAI_FUNC void LUAK_setlist (FuncState *fs, int base, int nelems, int tostore);


#endif
