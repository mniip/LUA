/*
** $Id: Lzio.h,v 1.26 2011/07/15 12:48:03 roberto Exp $
** Buffered streams
** See Copyright Notice in LUA.h
*/


#ifndef lzio_h
#define lzio_h

#include "LUA.h"

#include "Lmem.h"


#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : LUAZ_fill(z))


typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

#define LUAZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

#define LUAZ_buffer(buff)	((buff)->buffer)
#define LUAZ_sizebuffer(buff)	((buff)->buffsize)
#define LUAZ_bufflen(buff)	((buff)->n)

#define LUAZ_resetbuffer(buff) ((buff)->n = 0)


#define LUAZ_resizebuffer(L, buff, size) \
	(LUAM_reallocvector(L, (buff)->buffer, (buff)->buffsize, size, char), \
	(buff)->buffsize = size)

#define LUAZ_freebuffer(L, buff)	LUAZ_resizebuffer(L, buff, 0)


LUAI_FUNC char *LUAZ_openspace (LUA_State *L, Mbuffer *buff, size_t n);
LUAI_FUNC void LUAZ_init (LUA_State *L, ZIO *z, LUA_Reader reader,
                                        void *data);
LUAI_FUNC size_t LUAZ_read (ZIO* z, void* b, size_t n);	/* read next n bytes */



/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  LUA_Reader reader;		/* reader function */
  void* data;			/* additional data */
  LUA_State *L;			/* LUA state (for reader) */
};


LUAI_FUNC int LUAZ_fill (ZIO *z);

#endif
