#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "synth.h"

static int
get_synth_freq(int fd)
{
	printf("in get synth freq\n");
	return 0;
}

static int
set_synth_freq(int fd)
{
	char *freq = gettok();

	printf("in set synth freq\n");
	if (freq == NULL) {
		printf("expected frequency\n");
		return -2;
	}
	return 0;
}

static int
load_synth_freq(int fd)
{
	printf("in load synth freq\n");
	return 0;
}

static int
get_synth_edge(int fd)
{
	printf("in get synth edge\n");
	return 0;
}

static int
set_synth_edge(int fd)
{
	char *edge = gettok();

	if (edge == NULL) {
		printf("expected rising or falling\n");
		return -2;
	}
	printf("would set synth edge to %s\n", edge);
	return 0;
}

static int
get_synth_enable(int fd)
{
	printf("in get synth enable\n");
	return 0;
}

static int
set_synth_enable(int fd)
{
	char *enable = gettok();

	if (enable == NULL) {
		printf("expected yes nor no\n");
		return -2;
	}
	printf("would set enable to %s\n", enable);
	return 0;
}

int
get_synth(int fd)
{
	static struct node params[] = {
		{ "freq", "synth frequency", get_synth_freq },
		{ "edge", "synth on-time edge", get_synth_edge },
		{ "enable", "synth enable state", get_synth_enable },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_synth(int fd)
{
	static struct node params[] = {
		{ "freq", "synth generator frequency", set_synth_freq },
		{ "edge", "synth on-time edge", set_synth_edge },
		{ "enable", "synth enable state", set_synth_enable },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
load_synth(int fd)
{
	static struct node params[] = {
		{ "freq", "synth generator frequency", load_synth_freq },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
