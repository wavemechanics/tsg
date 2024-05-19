#include <stdio.h>
#include <strings.h>
#include "map.h"

char *
mapbyval(struct map *m, uint8_t val, char *def)
{
	struct map *p;

	for (p = m; p->desc; ++p)
		if (p->val == val)
			return p->desc;
	return def;
}

uint8_t
mapbydesc(struct map *m, char *desc, uint8_t def)
{
	struct map *p;

	for (p = m; p->desc; ++p)
		if (strcasecmp(p->desc, desc) == 0)
			return p->val;
	return def;
}

void
printmap(FILE *f, struct map *m, uint8_t flags)
{
	struct map *p;

	for (p = m; p->desc; ++p)
		fprintf(f, "%s\n", p->desc);
}

int
truthy(char *s)
{
	static struct map tab[] = {
		{ 0, "0" },
		{ 0, "n" },
		{ 0, "no" },
		{ 0, "false" },
		{ 1, "1" },
		{ 1, "y" },
		{ 1, "yes" },
		{ 1, "true" },
		{ 0, NULL }
	};

	int val = mapbydesc(tab, s, 2);
	return val == 2 ? -1 : val;
}
