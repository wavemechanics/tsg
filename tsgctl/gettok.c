#include <stdio.h>
#include "gettok.h"

static int tok_argc;
static char **tok_argv;

void
init_gettok(int argc, char **argv)
{
	tok_argc = /*--*/argc;
	tok_argv = /*++*/argv;
}

char *
gettok(void)
{
	if (tok_argc-- <= 0)
		return NULL;
	return *tok_argv++;
}
