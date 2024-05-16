#include <stdio.h>
#include "gettok.h"
#include "node.h"
#include "tsglib.h"
#include "timecode.h"

static int
get_format(int fd)
{
	printf("in get format\n");
	return 0;
}

static int
set_format(int fd)
{
	char *format = gettok();

	printf("in set format\n");
	if (format == NULL) {
		printf("expected format\n");
		return -2;
	}
	return 0;
}

static int
get_quality(int fd)
{
	printf("in get timecode quality\n");
	return 0;
}

static int
set_quality(int fd)
{
	char *quality = gettok();

	printf("in set quality\n");
	if (quality == NULL) {
		printf("expected quality\n");
		return -2;
	}
	return 0;
}

static int
get_calibration(int fd)
{
	printf("in get calibration\n");
	return 0;
}

static int
set_calibration(int fd)
{
	char *calibration = gettok();

	printf("in set calibration\n");
	if (calibration == NULL) {
		printf("expected calibration\n");
		return -2;
	}
	return 0;
}

static int
get_agc_delays(int fd)
{
	struct tsg_agc_delays delays;

	if (tsg_get_timecode_agc_delays(fd, &delays) != 0)
		return -1;
	printf("IRIG-A AGC delay: 0.%09ds\n", delays.irig_a_nsec);
	printf("IRIG-B AGC delay: 0.%09ds\n", delays.irig_b_nsec);
	return 0;
}

static int
set_agc_delays(int fd)
{
	char *agc_delays = gettok();

	printf("in set agc delays\n");
	if (agc_delays == NULL) {
		printf("expected agc delays\n");
		return -2;
	}
	return 0;
}

int
get_timecode(int fd)
{
	static struct node params[] = {
		{ "format", "timecode format", get_format },
		{ "quality", "timecode quality bits", get_quality },
		{ "calibration", "calibration code", get_calibration },
		{ "agc-delays", "AGC delays", get_agc_delays },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}

int
set_timecode(int fd)
{
	static struct node params[] = {
		{ "format", "timecode format", set_format },
		{ "quality", "use timecode quality", set_quality },
		{ "calibration", "start calibration", set_calibration },
		{ "agc-delays", "AGC delays", set_agc_delays },
		{ NULL, NULL, NULL },
	};

	return walk(params, fd);
}
