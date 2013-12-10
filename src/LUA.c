/*
** $Id: LUA.c,v 1.206 2012/09/29 20:07:06 roberto Exp $
** LUA stand-alone interpreter
** See Copyright Notice in LUA.h
*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LUA_c

#include "LUA.h"

#include "Lauxlib.h"
#include "LUAlib.h"


#if !defined(LUA_PROMPT)
#define LUA_PROMPT		"> "
#define LUA_PROMPT2		">> "
#endif

#if !defined(LUA_PROGNAME)
#define LUA_PROGNAME		"LUA"
#endif

#if !defined(LUA_MAXINPUT)
#define LUA_MAXINPUT		512
#endif

#if !defined(LUA_INIT)
#define LUA_INIT		"LUA_INIT"
#endif

#define LUA_INITVERSION  \
	LUA_INIT "_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR


/*
** LUA_stdin_is_tty detects whether the standard input is a 'tty' (that
** is, whether we're running LUA interactively).
*/
#if defined(LUA_USE_ISATTY)
#include <unistd.h>
#define LUA_stdin_is_tty()	isatty(0)
#elif defined(LUA_WIN)
#include <io.h>
#include <stdio.h>
#define LUA_stdin_is_tty()	_isatty(_fileno(stdin))
#else
#define LUA_stdin_is_tty()	1  /* assume stdin is a tty */
#endif


/*
** LUA_readline defines how to show a prompt and then read a line from
** the standard input.
** LUA_saveline defines how to "save" a read line in a "history".
** LUA_freeline defines how to free a line read by LUA_readline.
*/
#if defined(LUA_USE_READLINE)

#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#define LUA_readline(L,b,p)	((void)L, ((b)=readline(p)) != NULL)
#define LUA_saveline(L,idx) \
        if (LUA_rawlen(L,idx) > 0)  /* non-empty line? */ \
          add_history(LUA_tostring(L, idx));  /* add it to history */
#define LUA_freeline(L,b)	((void)L, free(b))

#elif !defined(LUA_readline)

#define LUA_readline(L,b,p) \
        ((void)L, fputs(p, stdout), fflush(stdout),  /* show prompt */ \
        fgets(b, LUA_MAXINPUT, stdin) != NULL)  /* get line */
#define LUA_saveline(L,idx)	{ (void)L; (void)idx; }
#define LUA_freeline(L,b)	{ (void)L; (void)b; }

#endif




static LUA_State *globalL = NULL;

static const char *progname = LUA_PROGNAME;



static void lstop (LUA_State *L, LUA_Debug *ar) {
  (void)ar;  /* unused arg. */
  LUA_sethook(L, NULL, 0, 0);
  LUAL_error(L, "interrupted!");
}


static void laction (int i) {
  signal(i, SIG_DFL); /* if another SIGINT happens before lstop,
                              terminate process (default action) */
  LUA_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}


static void print_usage (const char *badoption) {
  LUAi_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    LUAi_writestringerror("'%s' needs argument\n", badoption);
  else
    LUAi_writestringerror("unrecognized option '%s'\n", badoption);
  LUAi_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat  execute string " LUA_QL("stat") "\n"
  "  -i       enter interactive mode after executing " LUA_QL("script") "\n"
  "  -l name  require library " LUA_QL("name") "\n"
  "  -v       show version information\n"
  "  -E       ignore environment variables\n"
  "  --       stop handling options\n"
  "  -        stop handling options and execute stdin\n"
  ,
  progname);
}


static void l_message (const char *pname, const char *msg) {
  if (pname) LUAi_writestringerror("%s: ", pname);
  LUAi_writestringerror("%s\n", msg);
}


static int report (LUA_State *L, int status) {
  if (status != LUA_OK && !LUA_isnil(L, -1)) {
    const char *msg = LUA_tostring(L, -1);
    if (msg == NULL) msg = "(error object is not a string)";
    l_message(progname, msg);
    LUA_pop(L, 1);
    /* force a complete garbage collection in case of errors */
    LUA_gc(L, LUA_GCCOLLECT, 0);
  }
  return status;
}


/* the next function is called unprotected, so it must avoid errors */
static void finalreport (LUA_State *L, int status) {
  if (status != LUA_OK) {
    const char *msg = (LUA_type(L, -1) == LUA_TSTRING) ? LUA_tostring(L, -1)
                                                       : NULL;
    if (msg == NULL) msg = "(error object is not a string)";
    l_message(progname, msg);
    LUA_pop(L, 1);
  }
}


static int traceback (LUA_State *L) {
  const char *msg = LUA_tostring(L, 1);
  if (msg)
    LUAL_traceback(L, L, msg, 1);
  else if (!LUA_isnoneornil(L, 1)) {  /* is there an error object? */
    if (!LUAL_callmeta(L, 1, "__TOSTRING"))  /* try its 'tostring' metamethod */
      LUA_pushliteral(L, "(no error message)");
  }
  return 1;
}


static int docall (LUA_State *L, int narg, int nres) {
  int status;
  int base = LUA_gettop(L) - narg;  /* function index */
  LUA_pushcfunction(L, traceback);  /* push traceback function */
  LUA_insert(L, base);  /* put it under chunk and args */
  globalL = L;  /* to be available to 'laction' */
  signal(SIGINT, laction);
  status = LUA_pcall(L, narg, nres, base);
  signal(SIGINT, SIG_DFL);
  LUA_remove(L, base);  /* remove traceback function */
  return status;
}


static void print_version (void) {
  LUAi_writestring(LUA_COPYRIGHT, strlen(LUA_COPYRIGHT));
  LUAi_writeline();
}


static int getargs (LUA_State *L, char **argv, int n) {
  int narg;
  int i;
  int argc = 0;
  while (argv[argc]) argc++;  /* count total number of arguments */
  narg = argc - (n + 1);  /* number of arguments to the script */
  LUAL_checkstack(L, narg + 3, "too many arguments to script");
  for (i=n+1; i < argc; i++)
    LUA_pushstring(L, argv[i]);
  LUA_createtable(L, narg, n + 1);
  for (i=0; i < argc; i++) {
    LUA_pushstring(L, argv[i]);
    LUA_rawseti(L, -2, i - n);
  }
  return narg;
}


static int dofile (LUA_State *L, const char *name) {
  int status = LUAL_loadfile(L, name);
  if (status == LUA_OK) status = docall(L, 0, 0);
  return report(L, status);
}


static int dostring (LUA_State *L, const char *s, const char *name) {
  int status = LUAL_loadbuffer(L, s, strlen(s), name);
  if (status == LUA_OK) status = docall(L, 0, 0);
  return report(L, status);
}


static int dolibrary (LUA_State *L, const char *name) {
  int status;
  LUA_getglobal(L, "REQUIRE");
  LUA_pushstring(L, name);
  status = docall(L, 1, 1);  /* call 'require(name)' */
  if (status == LUA_OK)
    LUA_setglobal(L, name);  /* global[name] = require return */
  return report(L, status);
}


static const char *get_prompt (LUA_State *L, int firstline) {
  const char *p;
  LUA_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2");
  p = LUA_tostring(L, -1);
  if (p == NULL) p = (firstline ? LUA_PROMPT : LUA_PROMPT2);
  return p;
}

/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)

static int incomplete (LUA_State *L, int status) {
  if (status == LUA_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = LUA_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0) {
      LUA_pop(L, 1);
      return 1;
    }
  }
  return 0;  /* else... */
}


static int pushline (LUA_State *L, int firstline) {
  char buffer[LUA_MAXINPUT];
  char *b = buffer;
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  int readstatus = LUA_readline(L, b, prmt);
  LUA_pop(L, 1);  /* remove result from 'get_prompt' */
  if (readstatus == 0)
    return 0;  /* no input */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[l-1] = '\0';  /* remove it */
  if (firstline && b[0] == '=')  /* first line starts with `=' ? */
    LUA_pushfstring(L, "RETURN %s", b+1);  /* change it to `return' */
  else
    LUA_pushstring(L, b);
  LUA_freeline(L, b);
  return 1;
}


static int loadline (LUA_State *L) {
  int status;
  LUA_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  for (;;) {  /* repeat until gets a complete line */
    size_t l;
    const char *line = LUA_tolstring(L, 1, &l);
    status = LUAL_loadbuffer(L, line, l, "=STDIN");
    if (!incomplete(L, status)) break;  /* cannot try to add lines? */
    if (!pushline(L, 0))  /* no more input? */
      return -1;
    LUA_pushliteral(L, "\n");  /* add a new line... */
    LUA_insert(L, -2);  /* ...between the two lines */
    LUA_concat(L, 3);  /* join them */
  }
  LUA_saveline(L, 1);
  LUA_remove(L, 1);  /* remove line */
  return status;
}


static void dotty (LUA_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;
  while ((status = loadline(L)) != -1) {
    if (status == LUA_OK) status = docall(L, 0, LUA_MULTRET);
    report(L, status);
    if (status == LUA_OK && LUA_gettop(L) > 0) {  /* any result to print? */
      LUAL_checkstack(L, LUA_MINSTACK, "too many results to print");
      LUA_getglobal(L, "PRINT");
      LUA_insert(L, 1);
      if (LUA_pcall(L, LUA_gettop(L)-1, 0, 0) != LUA_OK)
        l_message(progname, LUA_pushfstring(L,
                               "error calling " LUA_QL("PRINT") " (%s)",
                               LUA_tostring(L, -1)));
    }
  }
  LUA_settop(L, 0);  /* clear stack */
  LUAi_writeline();
  progname = oldprogname;
}


static int handle_script (LUA_State *L, char **argv, int n) {
  int status;
  const char *fname;
  int narg = getargs(L, argv, n);  /* collect arguments */
  LUA_setglobal(L, "ARG");
  fname = argv[n];
  if (strcmp(fname, "-") == 0 && strcmp(argv[n-1], "--") != 0)
    fname = NULL;  /* stdin */
  status = LUAL_loadfile(L, fname);
  LUA_insert(L, -(narg+1));
  if (status == LUA_OK)
    status = docall(L, narg, LUA_MULTRET);
  else
    LUA_pop(L, narg);
  return report(L, status);
}


/* check that argument has no extra characters at the end */
#define noextrachars(x)		{if ((x)[2] != '\0') return -1;}


/* indices of various argument indicators in array args */
#define has_i		0	/* -i */
#define has_v		1	/* -v */
#define has_e		2	/* -e */
#define has_E		3	/* -E */

#define num_has		4	/* number of 'has_*' */


static int collectargs (char **argv, int *args) {
  int i;
  for (i = 1; argv[i] != NULL; i++) {
    if (argv[i][0] != '-')  /* not an option? */
        return i;
    switch (argv[i][1]) {  /* option */
      case '-':
        noextrachars(argv[i]);
        return (argv[i+1] != NULL ? i+1 : 0);
      case '\0':
        return i;
      case 'E':
        args[has_E] = 1;
        break;
      case 'i':
        noextrachars(argv[i]);
        args[has_i] = 1;  /* go through */
      case 'v':
        noextrachars(argv[i]);
        args[has_v] = 1;
        break;
      case 'e':
        args[has_e] = 1;  /* go through */
      case 'l':  /* both options need an argument */
        if (argv[i][2] == '\0') {  /* no concatenated argument? */
          i++;  /* try next 'argv' */
          if (argv[i] == NULL || argv[i][0] == '-')
            return -(i - 1);  /* no next argument or it is another option */
        }
        break;
      default:  /* invalid option; return its index... */
        return -i;  /* ...as a negative value */
    }
  }
  return 0;
}


static int runargs (LUA_State *L, char **argv, int n) {
  int i;
  for (i = 1; i < n; i++) {
    LUA_assert(argv[i][0] == '-');
    switch (argv[i][1]) {  /* option */
      case 'e': {
        const char *chunk = argv[i] + 2;
        if (*chunk == '\0') chunk = argv[++i];
        LUA_assert(chunk != NULL);
        if (dostring(L, chunk, "=(COMMAND LINE)") != LUA_OK)
          return 0;
        break;
      }
      case 'l': {
        const char *filename = argv[i] + 2;
        if (*filename == '\0') filename = argv[++i];
        LUA_assert(filename != NULL);
        if (dolibrary(L, filename) != LUA_OK)
          return 0;  /* stop if file fails */
        break;
      }
      default: break;
    }
  }
  return 1;
}


static int handle_LUAinit (LUA_State *L) {
  const char *name = "=" LUA_INITVERSION;
  const char *init = getenv(name + 1);
  if (init == NULL) {
    name = "=" LUA_INIT;
    init = getenv(name + 1);  /* try alternative name */
  }
  if (init == NULL) return LUA_OK;
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, name);
}


static int pmain (LUA_State *L) {
  int argc = (int)LUA_tointeger(L, 1);
  char **argv = (char **)LUA_touserdata(L, 2);
  int script;
  int args[num_has];
  args[has_i] = args[has_v] = args[has_e] = args[has_E] = 0;
  if (argv[0] && argv[0][0]) progname = argv[0];
  script = collectargs(argv, args);
  if (script < 0) {  /* invalid arg? */
    print_usage(argv[-script]);
    return 0;
  }
  if (args[has_v]) print_version();
  if (args[has_E]) {  /* option '-E'? */
    LUA_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
    LUA_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
  }
  /* open standard libraries */
  LUAL_checkversion(L);
  LUA_gc(L, LUA_GCSTOP, 0);  /* stop collector during initialization */
  LUAL_openlibs(L);  /* open libraries */
  LUA_gc(L, LUA_GCRESTART, 0);
  if (!args[has_E] && handle_LUAinit(L) != LUA_OK)
    return 0;  /* error running LUA_INIT */
  /* execute arguments -e and -l */
  if (!runargs(L, argv, (script > 0) ? script : argc)) return 0;
  /* execute main script (if there is one) */
  if (script && handle_script(L, argv, script) != LUA_OK) return 0;
  if (args[has_i])  /* -i option? */
    dotty(L);
  else if (script == 0 && !args[has_e] && !args[has_v]) {  /* no arguments? */
    if (LUA_stdin_is_tty()) {
      print_version();
      dotty(L);
    }
    else dofile(L, NULL);  /* executes stdin as a file */
  }
  LUA_pushboolean(L, 1);  /* signal no errors */
  return 1;
}


int main (int argc, char **argv) {
  int status, result;
  LUA_State *L = LUAL_newstate();  /* create state */
  if (L == NULL) {
    l_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  /* call 'pmain' in protected mode */
  LUA_pushcfunction(L, &pmain);
  LUA_pushinteger(L, argc);  /* 1st argument */
  LUA_pushlightuserdata(L, argv); /* 2nd argument */
  status = LUA_pcall(L, 2, 1, 0);
  result = LUA_toboolean(L, -1);  /* get result */
  finalreport(L, status);
  LUA_close(L);
  return (result && status == LUA_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

