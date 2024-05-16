#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "event.h"

static int
get_source(int fd)
{
	printf("in get source\n");
	return 0;
}

static int
set_source(int fd)
{
	char *source = gettok();

	printf("in set source\n");
	if (source == NULL) {
		printf("expected source\n");
		return -2;
	}
	return 0;
}

static int
get_time(int fd)
{
	printf("in get time\n");
	return 0;
}

int
get_event(int fd)
{
	static struct node params[] = {
		{ "source", "event timestamp source", get_source },
		{ "time", "timestamp", get_time },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_event(int fd)
{
	static struct node params[] = {
		{ "source", "event timestamp source", set_source },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
