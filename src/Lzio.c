/*
** $Id: lzio.c,v 1.35 2012/05/14 13:34:18 roberto Exp $
** Buffered streams
** See Copyright Notice in LUA.h
*/


#include <string.h>

#define lzio_c
#define LUA_CORE

#include "LUA.h"

#include "Llimits.h"
#include "Lmem.h"
#include "Lstate.h"
#include "Lzio.h"


int LUAZ_fill (ZIO *z) {
  size_t size;
  LUA_State *L = z->L;
  const char *buff;
  LUA_unlock(L);
  buff = z->reader(L, z->data, &size);
  LUA_lock(L);
  if (buff == NULL || size == 0)
    return EOZ;
  z->n = size - 1;  /* discount char being returned */
  z->p = buff;
  return cast_uchar(*(z->p++));
}


void LUAZ_init (LUA_State *L, ZIO *z, LUA_Reader reader, void *data) {
  z->L = L;
  z->reader = reader;
  z->data = data;
  z->n = 0;
  z->p = NULL;
}


/* --------------------------------------------------------------- read --- */
size_t LUAZ_read (ZIO *z, void *b, size_t n) {
  while (n) {
    size_t m;
    if (z->n == 0) {  /* no bytes in buffer? */
      if (LUAZ_fill(z) == EOZ)  /* try to read more */
        return n;  /* no more input; return number of missing bytes */
      else {
        z->n++;  /* LUAZ_fill consumed first byte; put it back */
        z->p--;
      }
    }
    m = (n <= z->n) ? n : z->n;  /* min. between n and z->n */
    memcpy(b, z->p, m);
    z->n -= m;
    z->p += m;
    b = (char *)b + m;
    n -= m;
  }
  return 0;
}

/* ------------------------------------------------------------------------ */
char *LUAZ_openspace (LUA_State *L, Mbuffer *buff, size_t n) {
  if (n > buff->buffsize) {
    if (n < LUA_MINBUFFER) n = LUA_MINBUFFER;
    LUAZ_resizebuffer(L, buff, n);
  }
  return buff->buffer;
}


