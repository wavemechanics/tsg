#ifndef	_MAP_H
#define	_MAP_H

#include <stdio.h>
#include <sys/types.h>

struct map {
	uint8_t val;
	char *desc;
};

char *mapbyval(struct map *m, uint8_t val, char *def);
uint8_t mapbydesc(struct map *m, char *desc, uint8_t def);
void printmap(FILE *f, struct map *m, uint8_t flags);

#endif
