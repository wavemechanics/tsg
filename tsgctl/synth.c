#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "map.h"
#include "tsglib.h"
#include "synth.h"

static int
get_synth_freq(int fd)
{
	uint32_t freq;

	if (tsg_get_synth_freq(fd, &freq) == -1)
		return -1;
	printf("synth freq: %lu Hz\n", (unsigned long)freq);
	return 0;
}

static int
set_synth_freq(int fd)
{
	char *tok = gettok();
	unsigned long n;
	uint32_t freq;
	char *msg = "set freq 1..1000000";

	if (tok == NULL) {
		puts(msg);
		return -2;
	}
	if (sscanf(tok, "%lu", &n) != 1) {
		puts(msg);
		return -2;
	}
	freq = n;

	return tsg_set_synth_freq(fd, &freq);
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
	uint8_t edge;

	if (tsg_get_synth_edge(fd, &edge) == -1)
		return -1;
	printf("synth edge: %s\n", edge ? "rising" : "falling");
	return 0;
}

static int
set_synth_edge(int fd)
{
	static struct map edge_map[] = {
		{ TSG_SYNTH_EDGE_RISING,	"rising" },
		{ 0,				"falling" },
		{ 0,				NULL }
	};
	char *tok = gettok();
	uint8_t edge;

	if (tok == NULL) {
		printmap(stdout, edge_map, 0);
		return -2;
	}
	edge = mapbydesc(edge_map, tok, 0xff);
	if (edge == 0xff) {
		printmap(stdout, edge_map, 0);
		return -2;
	}

	return tsg_set_synth_edge(fd, &edge);
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
		{ "freq", "synth frequency", set_synth_freq },
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
