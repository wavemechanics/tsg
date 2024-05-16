#include <stdio.h>
#include <string.h>
#include "gettok.h"
#include "node.h"

static void
help(struct node *np)
{
	struct node *p;

	for (p = np; p->name; ++p)
		printf("%-20s%s\n", p->name, p->desc);
}

int
walk(struct node *np, int fd)
{
	struct node *p;
	char *tok;

	if ((tok = gettok()) == NULL) {
		help(np);
		return -1;
	}
	if (strcmp(tok, "?") == 0 || strcmp(tok, "help") == 0) {
		help(np);
		return 0;
	}
	for (p = np; p->name; ++p)
		if (strcmp(tok, p->name) == 0)
			break;
	if (p->name == NULL) {
		printf("unexpected token %s\n", tok);
		return -1;
	}
	return (*p->f)(fd);
}
