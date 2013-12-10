# Makefile for building Lua
# See ../doc/readme.html for installation and customization instructions.

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Your platform. See PLATS for possible values.
PLAT= none

CC= gcc
CFLAGS= -O2 -Wall -DLUA_COMPAT_ALL $(SYSCFLAGS) $(MYCFLAGS)
LDFLAGS= $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS= -lm $(SYSLIBS) $(MYLIBS)

AR= ar rcu
RANLIB= ranlib
RM= rm -f

SYSCFLAGS=
SYSLDFLAGS=
SYSLIBS=

MYCFLAGS=
MYLDFLAGS=
MYLIBS=
MYOBJS=

# == END OF USER SETTINGS -- NO NEED TO CHANGE ANYTHING BELOW THIS LINE =======

PLATS= aix ansi bsd freebsd generic linux macosx mingw posix solaris

LUA_A=	libLUA.a
CORE_O=	Lapi.o Lcode.o Lctype.o Ldebug.o Ldo.o Ldump.o Lfunc.o Lgc.o Llex.o \
	Lmem.o Lobject.o Lopcodes.o Lparser.o Lstate.o Lstring.o Ltable.o \
	Ltm.o Lundump.o Lvm.o Lzio.o
LIB_O=	Lauxlib.o Lbaselib.o Lbitlib.o Lcorolib.o Ldblib.o Liolib.o \
	Lmathlib.o Loslib.o Lstrlib.o Ltablib.o Loadlib.o Linit.o
BASE_O= $(CORE_O) $(LIB_O) $(MYOBJS)

LUA_T=	LUA
LUA_O=	LUA.o

LUAC_T=	LUAC
LUAC_O=	LUAC.o

ALL_O= $(BASE_O) $(LUA_O) $(LUAC_O)
ALL_T= $(LUA_A) $(LUA_T) $(LUAC_T)
ALL_A= $(LUA_A)

# Targets start here.
default: $(PLAT)

all:	$(ALL_T)

o:	$(ALL_O)

a:	$(ALL_A)

$(LUA_A): $(BASE_O)
	$(AR) $@ $(BASE_O)
	$(RANLIB) $@

$(LUA_T): $(LUA_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(LUA_O) $(LUA_A) $(LIBS)

$(LUAC_T): $(LUAC_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(LUAC_O) $(LUA_A) $(LIBS)

clean:
	$(RM) $(ALL_T) $(ALL_O)

depend:
	@$(CC) $(CFLAGS) -MM l*.c

echo:
	@echo "PLAT= $(PLAT)"
	@echo "CC= $(CC)"
	@echo "CFLAGS= $(CFLAGS)"
	@echo "LDFLAGS= $(SYSLDFLAGS)"
	@echo "LIBS= $(LIBS)"
	@echo "AR= $(AR)"
	@echo "RANLIB= $(RANLIB)"
	@echo "RM= $(RM)"

# Convenience targets for popular platforms
ALL= all

none:
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

aix:
	$(MAKE) $(ALL) CC="xlc" CFLAGS="-O2 -DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-ldl" SYSLDFLAGS="-brtl -bexpall"

ansi:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_ANSI"

bsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-Wl,-E"

freebsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX" SYSLIBS="-Wl,-E -lreadline"

generic: $(ALL)

linux:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX" SYSLIBS="-Wl,-E -ldl -lreadline"

macosx:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_MACOSX" SYSLIBS="-lreadline"

mingw:
	$(MAKE) "LUA_A=LUA52.dll" "LUA_T=LUA.exe" \
	"AR=$(CC) -shared -o" "RANLIB=strip --strip-unneeded" \
	"SYSCFLAGS=-DLUA_BUILD_AS_DLL" "SYSLIBS=" "SYSLDFLAGS=-s" LUA.exe
	$(MAKE) "LUAC_T=LUAC.exe" LUAC.exe

posix:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX"

solaris:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-ldl"

# list targets that do not create files (but not all makes understand .PHONY)
.PHONY: all $(PLATS) default o a clean depend echo none

# DO NOT DELETE

Lapi.o: Lapi.c LUA.h LUAconf.h Lapi.h Llimits.h Lstate.h Lobject.h Ltm.h \
 Lzio.h Lmem.h Ldebug.h Ldo.h Lfunc.h Lgc.h Lstring.h Ltable.h Lundump.h \
 Lvm.h
Lauxlib.o: Lauxlib.c LUA.h LUAconf.h Lauxlib.h
Lbaselib.o: Lbaselib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Lbitlib.o: Lbitlib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Lcode.o: Lcode.c LUA.h LUAconf.h Lcode.h Llex.h Lobject.h Llimits.h \
 Lzio.h Lmem.h Lopcodes.h Lparser.h Ldebug.h Lstate.h Ltm.h Ldo.h Lgc.h \
 Lstring.h Ltable.h Lvm.h
Lcorolib.o: Lcorolib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Lctype.o: Lctype.c Lctype.h LUA.h LUAconf.h Llimits.h
Ldblib.o: Ldblib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Ldebug.o: Ldebug.c LUA.h LUAconf.h Lapi.h Llimits.h Lstate.h Lobject.h \
 Ltm.h Lzio.h Lmem.h Lcode.h Llex.h Lopcodes.h Lparser.h Ldebug.h Ldo.h \
 Lfunc.h Lstring.h Lgc.h Ltable.h Lvm.h
Ldo.o: Ldo.c LUA.h LUAconf.h Lapi.h Llimits.h Lstate.h Lobject.h Ltm.h \
 Lzio.h Lmem.h Ldebug.h Ldo.h Lfunc.h Lgc.h Lopcodes.h Lparser.h \
 Lstring.h Ltable.h Lundump.h Lvm.h
Ldump.o: Ldump.c LUA.h LUAconf.h Lobject.h Llimits.h Lstate.h Ltm.h \
 Lzio.h Lmem.h Lundump.h
Lfunc.o: Lfunc.c LUA.h LUAconf.h Lfunc.h Lobject.h Llimits.h Lgc.h \
 Lstate.h Ltm.h Lzio.h Lmem.h
Lgc.o: Lgc.c LUA.h LUAconf.h Ldebug.h Lstate.h Lobject.h Llimits.h Ltm.h \
 Lzio.h Lmem.h Ldo.h Lfunc.h Lgc.h Lstring.h Ltable.h
Linit.o: Linit.c LUA.h LUAconf.h LUAlib.h Lauxlib.h
Liolib.o: Liolib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Llex.o: Llex.c LUA.h LUAconf.h Lctype.h Llimits.h Ldo.h Lobject.h \
 Lstate.h Ltm.h Lzio.h Lmem.h Llex.h Lparser.h Lstring.h Lgc.h Ltable.h
Lmathlib.o: Lmathlib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Lmem.o: Lmem.c LUA.h LUAconf.h Ldebug.h Lstate.h Lobject.h Llimits.h \
 Ltm.h Lzio.h Lmem.h Ldo.h Lgc.h
Loadlib.o: Loadlib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Lobject.o: Lobject.c LUA.h LUAconf.h Lctype.h Llimits.h Ldebug.h Lstate.h \
 Lobject.h Ltm.h Lzio.h Lmem.h Ldo.h Lstring.h Lgc.h Lvm.h
Lopcodes.o: Lopcodes.c Lopcodes.h Llimits.h LUA.h LUAconf.h
Loslib.o: Loslib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Lparser.o: Lparser.c LUA.h LUAconf.h Lcode.h Llex.h Lobject.h Llimits.h \
 Lzio.h Lmem.h Lopcodes.h Lparser.h Ldebug.h Lstate.h Ltm.h Ldo.h Lfunc.h \
 Lstring.h Lgc.h Ltable.h
Lstate.o: Lstate.c LUA.h LUAconf.h Lapi.h Llimits.h Lstate.h Lobject.h \
 Ltm.h Lzio.h Lmem.h Ldebug.h Ldo.h Lfunc.h Lgc.h Llex.h Lstring.h \
 Ltable.h
Lstring.o: Lstring.c LUA.h LUAconf.h Lmem.h Llimits.h Lobject.h Lstate.h \
 Ltm.h Lzio.h Lstring.h Lgc.h
Lstrlib.o: Lstrlib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Ltable.o: Ltable.c LUA.h LUAconf.h Ldebug.h Lstate.h Lobject.h Llimits.h \
 Ltm.h Lzio.h Lmem.h Ldo.h Lgc.h Lstring.h Ltable.h Lvm.h
Ltablib.o: Ltablib.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
Ltm.o: Ltm.c LUA.h LUAconf.h Lobject.h Llimits.h Lstate.h Ltm.h Lzio.h \
 Lmem.h Lstring.h Lgc.h Ltable.h
LUA.o: LUA.c LUA.h LUAconf.h Lauxlib.h LUAlib.h
LUAC.o: LUAC.c LUA.h LUAconf.h Lauxlib.h Lobject.h Llimits.h Lstate.h \
 Ltm.h Lzio.h Lmem.h Lundump.h Ldebug.h Lopcodes.h
Lundump.o: Lundump.c LUA.h LUAconf.h Ldebug.h Lstate.h Lobject.h \
 Llimits.h Ltm.h Lzio.h Lmem.h Ldo.h Lfunc.h Lstring.h Lgc.h Lundump.h
Lvm.o: Lvm.c LUA.h LUAconf.h Ldebug.h Lstate.h Lobject.h Llimits.h Ltm.h \
 Lzio.h Lmem.h Ldo.h Lfunc.h Lgc.h Lopcodes.h Lstring.h Ltable.h Lvm.h
Lzio.o: Lzio.c LUA.h LUAconf.h Llimits.h Lmem.h Lstate.h Lobject.h Ltm.h \
 Lzio.h

