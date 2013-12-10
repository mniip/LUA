/*
** $Id: lstrlib.c,v 1.178 2012/08/14 18:12:34 roberto Exp $
** Standard library for string operations and pattern-matching
** See Copyright Notice in LUA.h
*/


#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lstrlib_c
#define LUA_LIB

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


/*
** maximum number of captures that a pattern can do during
** pattern-matching. This limit is arbitrary.
*/
#if !defined(LUA_MAXCAPTURES)
#define LUA_MAXCAPTURES		32
#endif


/* macro to `unsign' a character */
#define uchar(c)	((unsigned char)(c))



static int str_len (LUA_State *L) {
  size_t l;
  LUAL_checklstring(L, 1, &l);
  LUA_pushinteger(L, (LUA_Integer)l);
  return 1;
}


/* translate a relative string position: negative means back from end */
static size_t posrelat (ptrdiff_t pos, size_t len) {
  if (pos >= 0) return (size_t)pos;
  else if (0u - (size_t)pos > len) return 0;
  else return len - ((size_t)-pos) + 1;
}


static int str_sub (LUA_State *L) {
  size_t l;
  const char *s = LUAL_checklstring(L, 1, &l);
  size_t start = posrelat(LUAL_checkinteger(L, 2), l);
  size_t end = posrelat(LUAL_optinteger(L, 3, -1), l);
  if (start < 1) start = 1;
  if (end > l) end = l;
  if (start <= end)
    LUA_pushlstring(L, s + start - 1, end - start + 1);
  else LUA_pushliteral(L, "");
  return 1;
}


static int str_reverse (LUA_State *L) {
  size_t l, i;
  LUAL_Buffer b;
  const char *s = LUAL_checklstring(L, 1, &l);
  char *p = LUAL_buffinitsize(L, &b, l);
  for (i = 0; i < l; i++)
    p[i] = s[l - i - 1];
  LUAL_pushresultsize(&b, l);
  return 1;
}


static int str_lower (LUA_State *L) {
  size_t l;
  size_t i;
  LUAL_Buffer b;
  const char *s = LUAL_checklstring(L, 1, &l);
  char *p = LUAL_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = tolower(uchar(s[i]));
  LUAL_pushresultsize(&b, l);
  return 1;
}


static int str_upper (LUA_State *L) {
  size_t l;
  size_t i;
  LUAL_Buffer b;
  const char *s = LUAL_checklstring(L, 1, &l);
  char *p = LUAL_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = toupper(uchar(s[i]));
  LUAL_pushresultsize(&b, l);
  return 1;
}


/* reasonable limit to avoid arithmetic overflow */
#define MAXSIZE		((~(size_t)0) >> 1)

static int str_rep (LUA_State *L) {
  size_t l, lsep;
  const char *s = LUAL_checklstring(L, 1, &l);
  int n = LUAL_checkint(L, 2);
  const char *sep = LUAL_optlstring(L, 3, "", &lsep);
  if (n <= 0) LUA_pushliteral(L, "");
  else if (l + lsep < l || l + lsep >= MAXSIZE / n)  /* may overflow? */
    return LUAL_error(L, "resulting string too large");
  else {
    size_t totallen = n * l + (n - 1) * lsep;
    LUAL_Buffer b;
    char *p = LUAL_buffinitsize(L, &b, totallen);
    while (n-- > 1) {  /* first n-1 copies (followed by separator) */
      memcpy(p, s, l * sizeof(char)); p += l;
      if (lsep > 0) {  /* avoid empty 'memcpy' (may be expensive) */
        memcpy(p, sep, lsep * sizeof(char)); p += lsep;
      }
    }
    memcpy(p, s, l * sizeof(char));  /* last copy (not followed by separator) */
    LUAL_pushresultsize(&b, totallen);
  }
  return 1;
}


static int str_byte (LUA_State *L) {
  size_t l;
  const char *s = LUAL_checklstring(L, 1, &l);
  size_t posi = posrelat(LUAL_optinteger(L, 2, 1), l);
  size_t pose = posrelat(LUAL_optinteger(L, 3, posi), l);
  int n, i;
  if (posi < 1) posi = 1;
  if (pose > l) pose = l;
  if (posi > pose) return 0;  /* empty interval; return no values */
  n = (int)(pose -  posi + 1);
  if (posi + n <= pose)  /* (size_t -> int) overflow? */
    return LUAL_error(L, "string slice too long");
  LUAL_checkstack(L, n, "string slice too long");
  for (i=0; i<n; i++)
    LUA_pushinteger(L, uchar(s[posi+i-1]));
  return n;
}


static int str_char (LUA_State *L) {
  int n = LUA_gettop(L);  /* number of arguments */
  int i;
  LUAL_Buffer b;
  char *p = LUAL_buffinitsize(L, &b, n);
  for (i=1; i<=n; i++) {
    int c = LUAL_checkint(L, i);
    LUAL_argcheck(L, uchar(c) == c, i, "value out of range");
    p[i - 1] = uchar(c);
  }
  LUAL_pushresultsize(&b, n);
  return 1;
}


static int writer (LUA_State *L, const void* b, size_t size, void* B) {
  (void)L;
  LUAL_addlstring((LUAL_Buffer*) B, (const char *)b, size);
  return 0;
}


static int str_dump (LUA_State *L) {
  LUAL_Buffer b;
  LUAL_checktype(L, 1, LUA_TFUNCTION);
  LUA_settop(L, 1);
  LUAL_buffinit(L,&b);
  if (LUA_dump(L, writer, &b) != 0)
    return LUAL_error(L, "unable to dump given function");
  LUAL_pushresult(&b);
  return 1;
}



/*
** {======================================================
** PATTERN MATCHING
** =======================================================
*/


#define CAP_UNFINISHED	(-1)
#define CAP_POSITION	(-2)


typedef struct MatchState {
  int matchdepth;  /* control for recursive depth (to avoid C stack overflow) */
  const char *src_init;  /* init of source string */
  const char *src_end;  /* end ('\0') of source string */
  const char *p_end;  /* end ('\0') of pattern */
  LUA_State *L;
  int level;  /* total number of captures (finished or unfinished) */
  struct {
    const char *init;
    ptrdiff_t len;
  } capture[LUA_MAXCAPTURES];
} MatchState;


/* recursive function */
static const char *match (MatchState *ms, const char *s, const char *p);


/* maximum recursion depth for 'match' */
#if !defined(MAXCCALLS)
#define MAXCCALLS	200
#endif


#define L_ESC		'%'
#define SPECIALS	"^$*+?.([%-"


static int check_capture (MatchState *ms, int l) {
  l -= '1';
  if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED)
    return LUAL_error(ms->L, "invalid capture index %%%d", l + 1);
  return l;
}


static int capture_to_close (MatchState *ms) {
  int level = ms->level;
  for (level--; level>=0; level--)
    if (ms->capture[level].len == CAP_UNFINISHED) return level;
  return LUAL_error(ms->L, "invalid pattern capture");
}


static const char *classend (MatchState *ms, const char *p) {
  switch (*p++) {
    case L_ESC: {
      if (p == ms->p_end)
        LUAL_error(ms->L, "malformed pattern (ends with " LUA_QL("%%") ")");
      return p+1;
    }
    case '[': {
      if (*p == '^') p++;
      do {  /* look for a `]' */
        if (p == ms->p_end)
          LUAL_error(ms->L, "malformed pattern (missing " LUA_QL("]") ")");
        if (*(p++) == L_ESC && p < ms->p_end)
          p++;  /* skip escapes (e.g. `%]') */
      } while (*p != ']');
      return p+1;
    }
    default: {
      return p;
    }
  }
}


static int match_class (int c, int cl) {
  int res;
  switch (tolower(cl)) {
    case 'a' : res = isalpha(c); break;
    case 'c' : res = iscntrl(c); break;
    case 'd' : res = isdigit(c); break;
    case 'g' : res = isgraph(c); break;
    case 'l' : res = islower(c); break;
    case 'p' : res = ispunct(c); break;
    case 's' : res = isspace(c); break;
    case 'u' : res = isupper(c); break;
    case 'w' : res = isalnum(c); break;
    case 'x' : res = isxdigit(c); break;
    case 'z' : res = (c == 0); break;  /* deprecated option */
    default: return (cl == c);
  }
  return (islower(cl) ? res : !res);
}


static int matchbracketclass (int c, const char *p, const char *ec) {
  int sig = 1;
  if (*(p+1) == '^') {
    sig = 0;
    p++;  /* skip the `^' */
  }
  while (++p < ec) {
    if (*p == L_ESC) {
      p++;
      if (match_class(c, uchar(*p)))
        return sig;
    }
    else if ((*(p+1) == '-') && (p+2 < ec)) {
      p+=2;
      if (uchar(*(p-2)) <= c && c <= uchar(*p))
        return sig;
    }
    else if (uchar(*p) == c) return sig;
  }
  return !sig;
}


static int singlematch (MatchState *ms, const char *s, const char *p,
                        const char *ep) {
  if (s >= ms->src_end)
    return 0;
  else {
    int c = uchar(*s);
    switch (*p) {
      case '.': return 1;  /* matches any char */
      case L_ESC: return match_class(c, uchar(*(p+1)));
      case '[': return matchbracketclass(c, p, ep-1);
      default:  return (uchar(*p) == c);
    }
  }
}


static const char *matchbalance (MatchState *ms, const char *s,
                                   const char *p) {
  if (p >= ms->p_end - 1)
    LUAL_error(ms->L, "malformed pattern "
                      "(missing arguments to " LUA_QL("%%b") ")");
  if (*s != *p) return NULL;
  else {
    int b = *p;
    int e = *(p+1);
    int cont = 1;
    while (++s < ms->src_end) {
      if (*s == e) {
        if (--cont == 0) return s+1;
      }
      else if (*s == b) cont++;
    }
  }
  return NULL;  /* string ends out of balance */
}


static const char *max_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  ptrdiff_t i = 0;  /* counts maximum expand for item */
  while (singlematch(ms, s + i, p, ep))
    i++;
  /* keeps trying to match with the maximum repetitions */
  while (i>=0) {
    const char *res = match(ms, (s+i), ep+1);
    if (res) return res;
    i--;  /* else didn't match; reduce 1 repetition to try again */
  }
  return NULL;
}


static const char *min_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  for (;;) {
    const char *res = match(ms, s, ep+1);
    if (res != NULL)
      return res;
    else if (singlematch(ms, s, p, ep))
      s++;  /* try with one more repetition */
    else return NULL;
  }
}


static const char *start_capture (MatchState *ms, const char *s,
                                    const char *p, int what) {
  const char *res;
  int level = ms->level;
  if (level >= LUA_MAXCAPTURES) LUAL_error(ms->L, "too many captures");
  ms->capture[level].init = s;
  ms->capture[level].len = what;
  ms->level = level+1;
  if ((res=match(ms, s, p)) == NULL)  /* match failed? */
    ms->level--;  /* undo capture */
  return res;
}


static const char *end_capture (MatchState *ms, const char *s,
                                  const char *p) {
  int l = capture_to_close(ms);
  const char *res;
  ms->capture[l].len = s - ms->capture[l].init;  /* close capture */
  if ((res = match(ms, s, p)) == NULL)  /* match failed? */
    ms->capture[l].len = CAP_UNFINISHED;  /* undo capture */
  return res;
}


static const char *match_capture (MatchState *ms, const char *s, int l) {
  size_t len;
  l = check_capture(ms, l);
  len = ms->capture[l].len;
  if ((size_t)(ms->src_end-s) >= len &&
      memcmp(ms->capture[l].init, s, len) == 0)
    return s+len;
  else return NULL;
}


static const char *match (MatchState *ms, const char *s, const char *p) {
  if (ms->matchdepth-- == 0)
    LUAL_error(ms->L, "pattern too complex");
  init: /* using goto's to optimize tail recursion */
  if (p != ms->p_end) {  /* end of pattern? */
    switch (*p) {
      case '(': {  /* start capture */
        if (*(p + 1) == ')')  /* position capture? */
          s = start_capture(ms, s, p + 2, CAP_POSITION);
        else
          s = start_capture(ms, s, p + 1, CAP_UNFINISHED);
        break;
      }
      case ')': {  /* end capture */
        s = end_capture(ms, s, p + 1);
        break;
      }
      case '$': {
        if ((p + 1) != ms->p_end)  /* is the `$' the last char in pattern? */
          goto dflt;  /* no; go to default */
        s = (s == ms->src_end) ? s : NULL;  /* check end of string */
        break;
      }
      case L_ESC: {  /* escaped sequences not in the format class[*+?-]? */
        switch (*(p + 1)) {
          case 'b': {  /* balanced string? */
            s = matchbalance(ms, s, p + 2);
            if (s != NULL) {
              p += 4; goto init;  /* return match(ms, s, p + 4); */
            }  /* else fail (s == NULL) */
            break;
          }
          case 'f': {  /* frontier? */
            const char *ep; char previous;
            p += 2;
            if (*p != '[')
              LUAL_error(ms->L, "missing " LUA_QL("[") " after "
                                 LUA_QL("%%f") " in pattern");
            ep = classend(ms, p);  /* points to what is next */
            previous = (s == ms->src_init) ? '\0' : *(s - 1);
            if (!matchbracketclass(uchar(previous), p, ep - 1) &&
               matchbracketclass(uchar(*s), p, ep - 1)) {
              p = ep; goto init;  /* return match(ms, s, ep); */
            }
            s = NULL;  /* match failed */
            break;
          }
          case '0': case '1': case '2': case '3':
          case '4': case '5': case '6': case '7':
          case '8': case '9': {  /* capture results (%0-%9)? */
            s = match_capture(ms, s, uchar(*(p + 1)));
            if (s != NULL) {
              p += 2; goto init;  /* return match(ms, s, p + 2) */
            }
            break;
          }
          default: goto dflt;
        }
        break;
      }
      default: dflt: {  /* pattern class plus optional suffix */
        const char *ep = classend(ms, p);  /* points to optional suffix */
        /* does not match at least once? */
        if (!singlematch(ms, s, p, ep)) {
          if (*ep == '*' || *ep == '?' || *ep == '-') {  /* accept empty? */
            p = ep + 1; goto init;  /* return match(ms, s, ep + 1); */
          }
          else  /* '+' or no suffix */
            s = NULL;  /* fail */
        }
        else {  /* matched once */
          switch (*ep) {  /* handle optional suffix */
            case '?': {  /* optional */
              const char *res;
              if ((res = match(ms, s + 1, ep + 1)) != NULL)
                s = res;
              else {
                p = ep + 1; goto init;  /* else return match(ms, s, ep + 1); */
              }
              break;
            }
            case '+':  /* 1 or more repetitions */
              s++;  /* 1 match already done */
              /* go through */
            case '*':  /* 0 or more repetitions */
              s = max_expand(ms, s, p, ep);
              break;
            case '-':  /* 0 or more repetitions (minimum) */
              s = min_expand(ms, s, p, ep);
              break;
            default:  /* no suffix */
              s++; p = ep; goto init;  /* return match(ms, s + 1, ep); */
          }
        }
        break;
      }
    }
  }
  ms->matchdepth++;
  return s;
}



static const char *lmemfind (const char *s1, size_t l1,
                               const char *s2, size_t l2) {
  if (l2 == 0) return s1;  /* empty strings are everywhere */
  else if (l2 > l1) return NULL;  /* avoids a negative `l1' */
  else {
    const char *init;  /* to search for a `*s2' inside `s1' */
    l2--;  /* 1st char will be checked by `memchr' */
    l1 = l1-l2;  /* `s2' cannot be found after that */
    while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL) {
      init++;   /* 1st char is already checked */
      if (memcmp(init, s2+1, l2) == 0)
        return init-1;
      else {  /* correct `l1' and `s1' to try again */
        l1 -= init-s1;
        s1 = init;
      }
    }
    return NULL;  /* not found */
  }
}


static void push_onecapture (MatchState *ms, int i, const char *s,
                                                    const char *e) {
  if (i >= ms->level) {
    if (i == 0)  /* ms->level == 0, too */
      LUA_pushlstring(ms->L, s, e - s);  /* add whole match */
    else
      LUAL_error(ms->L, "invalid capture index");
  }
  else {
    ptrdiff_t l = ms->capture[i].len;
    if (l == CAP_UNFINISHED) LUAL_error(ms->L, "unfinished capture");
    if (l == CAP_POSITION)
      LUA_pushinteger(ms->L, ms->capture[i].init - ms->src_init + 1);
    else
      LUA_pushlstring(ms->L, ms->capture[i].init, l);
  }
}


static int push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
  LUAL_checkstack(ms->L, nlevels, "too many captures");
  for (i = 0; i < nlevels; i++)
    push_onecapture(ms, i, s, e);
  return nlevels;  /* number of strings pushed */
}


/* check whether pattern has no special characters */
static int nospecials (const char *p, size_t l) {
  size_t upto = 0;
  do {
    if (strpbrk(p + upto, SPECIALS))
      return 0;  /* pattern has a special character */
    upto += strlen(p + upto) + 1;  /* may have more after \0 */
  } while (upto <= l);
  return 1;  /* no special chars found */
}


static int str_find_aux (LUA_State *L, int find) {
  size_t ls, lp;
  const char *s = LUAL_checklstring(L, 1, &ls);
  const char *p = LUAL_checklstring(L, 2, &lp);
  size_t init = posrelat(LUAL_optinteger(L, 3, 1), ls);
  if (init < 1) init = 1;
  else if (init > ls + 1) {  /* start after string's end? */
    LUA_pushnil(L);  /* cannot find anything */
    return 1;
  }
  /* explicit request or no special characters? */
  if (find && (LUA_toboolean(L, 4) || nospecials(p, lp))) {
    /* do a plain search */
    const char *s2 = lmemfind(s + init - 1, ls - init + 1, p, lp);
    if (s2) {
      LUA_pushinteger(L, s2 - s + 1);
      LUA_pushinteger(L, s2 - s + lp);
      return 2;
    }
  }
  else {
    MatchState ms;
    const char *s1 = s + init - 1;
    int anchor = (*p == '^');
    if (anchor) {
      p++; lp--;  /* skip anchor character */
    }
    ms.L = L;
    ms.matchdepth = MAXCCALLS;
    ms.src_init = s;
    ms.src_end = s + ls;
    ms.p_end = p + lp;
    do {
      const char *res;
      ms.level = 0;
      LUA_assert(ms.matchdepth == MAXCCALLS);
      if ((res=match(&ms, s1, p)) != NULL) {
        if (find) {
          LUA_pushinteger(L, s1 - s + 1);  /* start */
          LUA_pushinteger(L, res - s);   /* end */
          return push_captures(&ms, NULL, 0) + 2;
        }
        else
          return push_captures(&ms, s1, res);
      }
    } while (s1++ < ms.src_end && !anchor);
  }
  LUA_pushnil(L);  /* not found */
  return 1;
}


static int str_find (LUA_State *L) {
  return str_find_aux(L, 1);
}


static int str_match (LUA_State *L) {
  return str_find_aux(L, 0);
}


static int gmatch_aux (LUA_State *L) {
  MatchState ms;
  size_t ls, lp;
  const char *s = LUA_tolstring(L, LUA_upvalueindex(1), &ls);
  const char *p = LUA_tolstring(L, LUA_upvalueindex(2), &lp);
  const char *src;
  ms.L = L;
  ms.matchdepth = MAXCCALLS;
  ms.src_init = s;
  ms.src_end = s+ls;
  ms.p_end = p + lp;
  for (src = s + (size_t)LUA_tointeger(L, LUA_upvalueindex(3));
       src <= ms.src_end;
       src++) {
    const char *e;
    ms.level = 0;
    LUA_assert(ms.matchdepth == MAXCCALLS);
    if ((e = match(&ms, src, p)) != NULL) {
      LUA_Integer newstart = e-s;
      if (e == src) newstart++;  /* empty match? go at least one position */
      LUA_pushinteger(L, newstart);
      LUA_replace(L, LUA_upvalueindex(3));
      return push_captures(&ms, src, e);
    }
  }
  return 0;  /* not found */
}


static int gmatch (LUA_State *L) {
  LUAL_checkstring(L, 1);
  LUAL_checkstring(L, 2);
  LUA_settop(L, 2);
  LUA_pushinteger(L, 0);
  LUA_pushcclosure(L, gmatch_aux, 3);
  return 1;
}


static void add_s (MatchState *ms, LUAL_Buffer *b, const char *s,
                                                   const char *e) {
  size_t l, i;
  const char *news = LUA_tolstring(ms->L, 3, &l);
  for (i = 0; i < l; i++) {
    if (news[i] != L_ESC)
      LUAL_addchar(b, news[i]);
    else {
      i++;  /* skip ESC */
      if (!isdigit(uchar(news[i]))) {
        if (news[i] != L_ESC)
          LUAL_error(ms->L, "invalid use of " LUA_QL("%c")
                           " in replacement string", L_ESC);
        LUAL_addchar(b, news[i]);
      }
      else if (news[i] == '0')
          LUAL_addlstring(b, s, e - s);
      else {
        push_onecapture(ms, news[i] - '1', s, e);
        LUAL_addvalue(b);  /* add capture to accumulated result */
      }
    }
  }
}


static void add_value (MatchState *ms, LUAL_Buffer *b, const char *s,
                                       const char *e, int tr) {
  LUA_State *L = ms->L;
  switch (tr) {
    case LUA_TFUNCTION: {
      int n;
      LUA_pushvalue(L, 3);
      n = push_captures(ms, s, e);
      LUA_call(L, n, 1);
      break;
    }
    case LUA_TTABLE: {
      push_onecapture(ms, 0, s, e);
      LUA_gettable(L, 3);
      break;
    }
    default: {  /* LUA_TNUMBER or LUA_TSTRING */
      add_s(ms, b, s, e);
      return;
    }
  }
  if (!LUA_toboolean(L, -1)) {  /* nil or false? */
    LUA_pop(L, 1);
    LUA_pushlstring(L, s, e - s);  /* keep original text */
  }
  else if (!LUA_isstring(L, -1))
    LUAL_error(L, "invalid replacement value (a %s)", LUAL_typename(L, -1));
  LUAL_addvalue(b);  /* add result to accumulator */
}


static int str_gsub (LUA_State *L) {
  size_t srcl, lp;
  const char *src = LUAL_checklstring(L, 1, &srcl);
  const char *p = LUAL_checklstring(L, 2, &lp);
  int tr = LUA_type(L, 3);
  size_t max_s = LUAL_optinteger(L, 4, srcl+1);
  int anchor = (*p == '^');
  size_t n = 0;
  MatchState ms;
  LUAL_Buffer b;
  LUAL_argcheck(L, tr == LUA_TNUMBER || tr == LUA_TSTRING ||
                   tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                      "string/function/table expected");
  LUAL_buffinit(L, &b);
  if (anchor) {
    p++; lp--;  /* skip anchor character */
  }
  ms.L = L;
  ms.matchdepth = MAXCCALLS;
  ms.src_init = src;
  ms.src_end = src+srcl;
  ms.p_end = p + lp;
  while (n < max_s) {
    const char *e;
    ms.level = 0;
    LUA_assert(ms.matchdepth == MAXCCALLS);
    e = match(&ms, src, p);
    if (e) {
      n++;
      add_value(&ms, &b, src, e, tr);
    }
    if (e && e>src) /* non empty match? */
      src = e;  /* skip it */
    else if (src < ms.src_end)
      LUAL_addchar(&b, *src++);
    else break;
    if (anchor) break;
  }
  LUAL_addlstring(&b, src, ms.src_end-src);
  LUAL_pushresult(&b);
  LUA_pushinteger(L, n);  /* number of substitutions */
  return 2;
}

/* }====================================================== */



/*
** {======================================================
** STRING FORMAT
** =======================================================
*/

/*
** LUA_INTFRMLEN is the length modifier for integer conversions in
** 'string.format'; LUA_INTFRM_T is the integer type corresponding to
** the previous length
*/
#if !defined(LUA_INTFRMLEN)	/* { */
#if defined(LUA_USE_LONGLONG)

#define LUA_INTFRMLEN		"ll"
#define LUA_INTFRM_T		long long

#else

#define LUA_INTFRMLEN		"l"
#define LUA_INTFRM_T		long

#endif
#endif				/* } */


/*
** LUA_FLTFRMLEN is the length modifier for float conversions in
** 'string.format'; LUA_FLTFRM_T is the float type corresponding to
** the previous length
*/
#if !defined(LUA_FLTFRMLEN)

#define LUA_FLTFRMLEN		""
#define LUA_FLTFRM_T		double

#endif


/* maximum size of each formatted item (> len(format('%99.99f', -1e308))) */
#define MAX_ITEM	512
/* valid flags in a format specification */
#define FLAGS	"-+ #0"
/*
** maximum size of each format specification (such as '%-099.99d')
** (+10 accounts for %99.99x plus margin of error)
*/
#define MAX_FORMAT	(sizeof(FLAGS) + sizeof(LUA_INTFRMLEN) + 10)


static void addquoted (LUA_State *L, LUAL_Buffer *b, int arg) {
  size_t l;
  const char *s = LUAL_checklstring(L, arg, &l);
  LUAL_addchar(b, '"');
  while (l--) {
    if (*s == '"' || *s == '\\' || *s == '\n') {
      LUAL_addchar(b, '\\');
      LUAL_addchar(b, *s);
    }
    else if (*s == '\0' || iscntrl(uchar(*s))) {
      char buff[10];
      if (!isdigit(uchar(*(s+1))))
        sprintf(buff, "\\%d", (int)uchar(*s));
      else
        sprintf(buff, "\\%03d", (int)uchar(*s));
      LUAL_addstring(b, buff);
    }
    else
      LUAL_addchar(b, *s);
    s++;
  }
  LUAL_addchar(b, '"');
}

static const char *scanformat (LUA_State *L, const char *strfrmt, char *form) {
  const char *p = strfrmt;
  while (*p != '\0' && strchr(FLAGS, *p) != NULL) p++;  /* skip flags */
  if ((size_t)(p - strfrmt) >= sizeof(FLAGS)/sizeof(char))
    LUAL_error(L, "invalid format (repeated flags)");
  if (isdigit(uchar(*p))) p++;  /* skip width */
  if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
  if (*p == '.') {
    p++;
    if (isdigit(uchar(*p))) p++;  /* skip precision */
    if (isdigit(uchar(*p))) p++;  /* (2 digits at most) */
  }
  if (isdigit(uchar(*p)))
    LUAL_error(L, "invalid format (width or precision too long)");
  *(form++) = '%';
  memcpy(form, strfrmt, (p - strfrmt + 1) * sizeof(char));
  form += p - strfrmt + 1;
  *form = '\0';
  return p;
}


/*
** add length modifier into formats
*/
static void addlenmod (char *form, const char *lenmod) {
  size_t l = strlen(form);
  size_t lm = strlen(lenmod);
  char spec = form[l - 1];
  strcpy(form + l - 1, lenmod);
  form[l + lm - 1] = spec;
  form[l + lm] = '\0';
}


static int str_format (LUA_State *L) {
  int top = LUA_gettop(L);
  int arg = 1;
  size_t sfl;
  const char *strfrmt = LUAL_checklstring(L, arg, &sfl);
  const char *strfrmt_end = strfrmt+sfl;
  LUAL_Buffer b;
  LUAL_buffinit(L, &b);
  while (strfrmt < strfrmt_end) {
    if (*strfrmt != L_ESC)
      LUAL_addchar(&b, *strfrmt++);
    else if (*++strfrmt == L_ESC)
      LUAL_addchar(&b, *strfrmt++);  /* %% */
    else { /* format item */
      char form[MAX_FORMAT];  /* to store the format (`%...') */
      char *buff = LUAL_prepbuffsize(&b, MAX_ITEM);  /* to put formatted item */
      int nb = 0;  /* number of bytes in added item */
      if (++arg > top)
        LUAL_argerror(L, arg, "no value");
      strfrmt = scanformat(L, strfrmt, form);
      switch (*strfrmt++) {
        case 'c': {
          nb = sprintf(buff, form, LUAL_checkint(L, arg));
          break;
        }
        case 'd': case 'i': {
          LUA_Number n = LUAL_checknumber(L, arg);
          LUA_INTFRM_T ni = (LUA_INTFRM_T)n;
          LUA_Number diff = n - (LUA_Number)ni;
          LUAL_argcheck(L, -1 < diff && diff < 1, arg,
                        "not a number in proper range");
          addlenmod(form, LUA_INTFRMLEN);
          nb = sprintf(buff, form, ni);
          break;
        }
        case 'o': case 'u': case 'x': case 'X': {
          LUA_Number n = LUAL_checknumber(L, arg);
          unsigned LUA_INTFRM_T ni = (unsigned LUA_INTFRM_T)n;
          LUA_Number diff = n - (LUA_Number)ni;
          LUAL_argcheck(L, -1 < diff && diff < 1, arg,
                        "not a non-negative number in proper range");
          addlenmod(form, LUA_INTFRMLEN);
          nb = sprintf(buff, form, ni);
          break;
        }
        case 'e': case 'E': case 'f':
#if defined(LUA_USE_AFORMAT)
        case 'a': case 'A':
#endif
        case 'g': case 'G': {
          addlenmod(form, LUA_FLTFRMLEN);
          nb = sprintf(buff, form, (LUA_FLTFRM_T)LUAL_checknumber(L, arg));
          break;
        }
        case 'q': {
          addquoted(L, &b, arg);
          break;
        }
        case 's': {
          size_t l;
          const char *s = LUAL_tolstring(L, arg, &l);
          if (!strchr(form, '.') && l >= 100) {
            /* no precision and string is too long to be formatted;
               keep original string */
            LUAL_addvalue(&b);
            break;
          }
          else {
            nb = sprintf(buff, form, s);
            LUA_pop(L, 1);  /* remove result from 'LUAL_tolstring' */
            break;
          }
        }
        default: {  /* also treat cases `pnLlh' */
          return LUAL_error(L, "invalid option " LUA_QL("%%%c") " to "
                               LUA_QL("FORMAT"), *(strfrmt - 1));
        }
      }
      LUAL_addsize(&b, nb);
    }
  }
  LUAL_pushresult(&b);
  return 1;
}

/* }====================================================== */


static const LUAL_Reg strlib[] = {
  {"BYTE", str_byte},
  {"CHAR", str_char},
  {"DUMP", str_dump},
  {"FIND", str_find},
  {"FORMAT", str_format},
  {"GMATCH", gmatch},
  {"GSUB", str_gsub},
  {"LEN", str_len},
  {"LOWER", str_lower},
  {"MATCH", str_match},
  {"REP", str_rep},
  {"REVERSE", str_reverse},
  {"SUB", str_sub},
  {"UPPER", str_upper},
  {NULL, NULL}
};


static void createmetatable (LUA_State *L) {
  LUA_createtable(L, 0, 1);  /* table to be metatable for strings */
  LUA_pushliteral(L, "");  /* dummy string */
  LUA_pushvalue(L, -2);  /* copy table */
  LUA_setmetatable(L, -2);  /* set table as metatable for strings */
  LUA_pop(L, 1);  /* pop dummy string */
  LUA_pushvalue(L, -2);  /* get string library */
  LUA_setfield(L, -2, "__INDEX");  /* metatable.__index = string */
  LUA_pop(L, 1);  /* pop metatable */
}


/*
** Open string library
*/
LUAMOD_API int LUAopen_string (LUA_State *L) {
  LUAL_newlib(L, strlib);
  createmetatable(L);
  return 1;
}

