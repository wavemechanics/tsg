#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "tsglib.h"
#include "compare.h"

static int
get_time(int fd)
{
	struct tsg_compare_time t;

	if (tsg_get_compare_time(fd, &t) != 0)
		return -1;
	printf(
		"%03u-%02u:%02u:%02u.%06u/%u\n",
		t.day,
		t.hour,
		t.min,
		t.sec,
		t.usec,
		t.mask
	);
	return 0;
}

static int
set_time(int fd)
{
	char *tok = gettok();
	int fields;
	unsigned day, hour, min, sec, usec, mask;
	char *msg = "compare time looks like DDD-HH:MM:SS.mmmuuu/mask";

	if (tok == NULL) {
		puts(msg);
		return -2;
	}
	fields = sscanf(
		tok,
		"%u-%u:%u:%u.%u/%u",
		&day,
		&hour,
		&min,
		&sec,
		&usec,
		&mask
	);
	if (fields != 6) {
		puts(msg);
		return -2;
	}

	struct tsg_compare_time t = {
		.day = day,
		.hour = hour,
		.min = min,
		.sec = sec,
		.usec = usec,
		.mask = mask,
	};
	return tsg_set_compare_time(fd, &t);
}

int
get_compare(int fd)
{
	static struct node params[] = {
		{ "time", "compare time and mask", get_time },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_compare(int fd)
{
	static struct node params[] = {
		{ "time", "compare time and mask", set_time },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
