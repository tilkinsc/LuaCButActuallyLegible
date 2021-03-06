/* lua515
** $Id: luac.c,v 1.54 2006/06/02 17:37:11 lhf Exp $
** Lua compiler (saves bytecodes to files; also list bytecodes)
** See Copyright Notice in lua.h
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define luac_c
#define LUA_CORE

#include "lua.h"
#include "lauxlib.h"

#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstring.h"
#include "lundump.h"



#define PROGNAME	"luac"					/* default program name */
#define OUTPUT		PROGNAME ".out"			/* default output file */

static int listing = 0;						/* list bytecodes? */
static int dumping = 1;						/* dump bytecodes? */
static int stripping = 0;					/* strip debug information? */
static char Output[] = { OUTPUT };			/* default output file name */
static const char* output = Output;			/* actual output file name */
static const char* progname = PROGNAME;		/* actual program name */



static void fatal(const char* message)
{
	fprintf(stderr, "%s: %s\n", progname, message);
	exit(EXIT_FAILURE);
}

static void cannot(const char* what)
{
	fprintf(stderr, "%s: cannot %s %s: %s\n", progname, what, output, strerror(errno));
	exit(EXIT_FAILURE);
}

static void usage(const char* message)
{
	if (*message == '-')
		fprintf(stderr, "%s: unrecognized option " LUA_QS "\n", progname, message);
	else
		fprintf(stderr, "%s: %s\n", progname, message);
	
	fprintf(stderr,
		"usage: %s [options] [filenames].\n"
		"Available options are:\n"
		"	-		 process stdin\n"
		"	-l		 list\n"
		"	-o name	 output to file " LUA_QL("name") " (default is \"%s\")\n"
		"	-p		 parse only\n"
		"	-s		 strip debug information\n"
		"	-v		 show version information\n"
		"	--		 stop handling options\n",
		progname, Output);
	
	exit(EXIT_FAILURE);
}




static int doargs(int argc, char* argv[])
{
	
	#define IS(s)	(strcmp(argv[i], s) == 0)
	
	if (argv[0] != NULL && *argv[0] != 0)
		progname = argv[0];
	
	int i;
	int version = 0;
	for (i = 1; i < argc; i++) {
		if (*argv[i] != '-') {		/* end of options; keep it */
			break;
		} else if (IS("--")) {		/* end of options; skip it */
			++i;
			if (version)
				++version;
			break;
		} else if (IS("-")) {		/* end of options; use stdin */
			break;
		} else if (IS("-l")) {		/* list */
			++listing;
		} else if (IS("-o")) {		/* output file */
			output = argv[++i];
			if (output == NULL || *output == 0)
				usage(LUA_QL("-o") " needs argument");
			if (IS("-"))
				output = NULL;
		} else if (IS("-p")) {			/* parse only */
			dumping = 0;
		} else if (IS("-s")) {			/* strip debug information */
			stripping = 1;
		} else if (IS("-v")) {			/* show version */
			++version;
		} else {					/* unknown option */
			usage(argv[i]);
		}
	}
	
	if (i == argc && (listing || !dumping)) {
		dumping = 0;
		argv[--i] = Output;
	}
	
	if (version) {
		printf("%s  %s\n", LUA_RELEASE, LUA_COPYRIGHT);
		if (version == (argc - 1))
			exit(EXIT_SUCCESS);
	}
	
	return i;
	
	#undef IS
	
}


static const Proto* combine(lua_State* L, int n)
{
	
	#define toproto(L, i) (clvalue(L->top + (i))->l.p)
	
	if (n == 1) {
		return toproto(L, -1);
	} else {
		Proto* f = luaF_newproto(L);
		setptvalue2s(L, L->top, f);
		incr_top(L);
		f->source = luaS_newliteral(L, "=(" PROGNAME ")");
		f->maxstacksize = 1;
		
		int pc;
		pc = 2*n + 1;
		f->code = luaM_newvector(L, pc, Instruction);
		f->sizecode = pc;
		f->p = luaM_newvector(L, n, Proto*);
		f->sizep = n;
		pc = 0;
		
		int i;
		for (i = 0; i < n; i++) {
			f->p[i] = toproto(L, i - n - 1);
			f->code[pc++] = CREATE_ABx(OP_CLOSURE, 0, i);
			f->code[pc++] = CREATE_ABC(OP_CALL, 0, 1, 1);
		}
		
		f->code[pc++] = CREATE_ABC(OP_RETURN, 0, 1, 0);
		return f;
	}
	
	#undef toproto
	
}

static int writer(lua_State* L, const void* p, size_t size, void* u)
{
	UNUSED(L);
	return (fwrite(p, size, 1, (FILE*)u) != 1) && (size != 0);
}

struct Smain {
	int argc;
	char** argv;
};

static int pmain(lua_State* L)
{
	struct Smain* s = (struct Smain*) lua_touserdata(L, 1);
	int argc = s->argc;
	char** argv = s->argv;
	
	if (!lua_checkstack(L, argc))
		fatal("too many input files");
	
	int i;
	for (i = 0; i < argc; i++) {
		const char* filename = IS("-") ? NULL : argv[i];
		if (luaL_loadfile(L, filename) != 0)
			fatal(lua_tostring(L, -1));
	}
	
	const Proto* f;
	f = combine(L, argc);
	
	if (listing)
		luaU_print(f, listing > 1);
	
	if (dumping) {
		FILE* D = (output == NULL) ? stdout : fopen(output, "wb");
		if (D == NULL)
			cannot("open");
		
		lua_lock(L);
		luaU_dump(L, f, writer, D, stripping);
		lua_unlock(L);
		
		if (ferror(D))
			cannot("write");
		
		if (fclose(D))
			cannot("close");
	}
	
	return 0;
}

int main(int argc, char* argv[])
{
	int i = doargs(argc, argv);
	argc -= i;
	argv += i;
	
	if (argc <= 0)
		usage("no input files given");
	
	lua_State* L;
	L = lua_open();
	if (L == NULL)
		fatal("not enough memory for state");
	
	struct Smain s;
	s.argc = argc;
	s.argv = argv;
	
	if (lua_cpcall(L, pmain, &s) != 0)
		fatal(lua_tostring(L, -1));
	
	lua_close(L);
	
	return EXIT_SUCCESS;
}

