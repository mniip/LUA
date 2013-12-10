/*
** $Id: ltm.c,v 2.14 2011/06/02 19:31:40 roberto Exp $
** Tag methods
** See Copyright Notice in LUA.h
*/


#include <string.h>

#define ltm_c
#define LUA_CORE

#include "LUA.h"

#include "Lobject.h"
#include "Lstate.h"
#include "Lstring.h"
#include "Ltable.h"
#include "Ltm.h"


static const char udatatypename[] = "USERDATA";

LUAI_DDEF const char *const LUAT_typenames_[LUA_TOTALTAGS] = {
  "NO VALUE",
  "NIL", "BOOLEAN", udatatypename, "NUMBER",
  "STRING", "TABLE", "FUNCTION", udatatypename, "THREAD",
  "PROTO", "UPVAL"  /* these last two cases are used for tests only */
};


void LUAT_init (LUA_State *L) {
  static const char *const LUAT_eventname[] = {  /* ORDER TM */
    "__INDEX", "__NEWINDEX",
    "__GC", "__MODE", "__LEN", "__EQ",
    "__ADD", "__SUB", "__MUL", "__DIV", "__MOD",
    "__POW", "__UNM", "__LT", "__LE",
    "__CONCAT", "__CALL"
  };
  int i;
  for (i=0; i<TM_N; i++) {
    G(L)->tmname[i] = LUAS_new(L, LUAT_eventname[i]);
    LUAS_fix(G(L)->tmname[i]);  /* never collect these names */
  }
}


/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods
*/
const TValue *LUAT_gettm (Table *events, TMS event, TString *ename) {
  const TValue *tm = LUAH_getstr(events, ename);
  LUA_assert(event <= TM_EQ);
  if (ttisnil(tm)) {  /* no tag method? */
    events->flags |= cast_byte(1u<<event);  /* cache this fact */
    return NULL;
  }
  else return tm;
}


const TValue *LUAT_gettmbyobj (LUA_State *L, const TValue *o, TMS event) {
  Table *mt;
  switch (ttypenv(o)) {
    case LUA_TTABLE:
      mt = hvalue(o)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(o)->metatable;
      break;
    default:
      mt = G(L)->mt[ttypenv(o)];
  }
  return (mt ? LUAH_getstr(mt, G(L)->tmname[event]) : LUAO_nilobject);
}

