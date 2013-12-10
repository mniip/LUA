/*
** $Id: Lmem.h,v 1.40 2013/02/20 14:08:21 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in LUA.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "Llimits.h"
#include "LUA.h"


/*
** This macro avoids the runtime division MAX_SIZET/(e), as 'e' is
** always constant.
** The macro is somewhat complex to avoid warnings:
** +1 avoids warnings of "comparison has constant result";
** cast to 'void' avoids warnings of "value unused".
*/
#define LUAM_reallocv(L,b,on,n,e) \
  (cast(void, \
     (cast(size_t, (n)+1) > MAX_SIZET/(e)) ? (LUAM_toobig(L), 0) : 0), \
   LUAM_realloc_(L, (b), (on)*(e), (n)*(e)))

#define LUAM_freemem(L, b, s)	LUAM_realloc_(L, (b), (s), 0)
#define LUAM_free(L, b)		LUAM_realloc_(L, (b), sizeof(*(b)), 0)
#define LUAM_freearray(L, b, n)   LUAM_reallocv(L, (b), n, 0, sizeof((b)[0]))

#define LUAM_malloc(L,s)	LUAM_realloc_(L, NULL, 0, (s))
#define LUAM_new(L,t)		cast(t *, LUAM_malloc(L, sizeof(t)))
#define LUAM_newvector(L,n,t) \
		cast(t *, LUAM_reallocv(L, NULL, 0, n, sizeof(t)))

#define LUAM_newobject(L,tag,s)	LUAM_realloc_(L, NULL, tag, (s))

#define LUAM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, LUAM_growaux_(L,v,&(size),sizeof(t),limit,e)))

#define LUAM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, LUAM_reallocv(L, v, oldn, n, sizeof(t))))

LUAI_FUNC l_noret LUAM_toobig (LUA_State *L);

/* not to be called directly */
LUAI_FUNC void *LUAM_realloc_ (LUA_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUAI_FUNC void *LUAM_growaux_ (LUA_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *what);

#endif

